#include "ui/file_browser/file_browser.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>

#include "ui/file_browser/directory_scanner.h"
#include "ui/file_browser/artwork.h"
#include "ui/ui.h"
#include "ui/input/scrolling.h"
#include "imgui_internal.h"
#include "renderer/styling/platform_styling.h"
#include "ui/icons.h"
#include "resyne/ui/toolbar/toolbar.h"

namespace UI::FileBrowser {

namespace {

std::filesystem::path filesystemPath(const std::string& path) {
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

void rebuildFilter(State& state) {
    state.visibleFiles.clear();
    for (size_t index = 0; index < state.files.size(); ++index) {
        if (state.filter.PassFilter(state.files[index].name.c_str())) state.visibleFiles.push_back(static_cast<int>(index));
    }
    state.filterDirty = false;
}

void drawWrappingText(const std::string& text, float elapsed, const ImVec4& clip, float y, ImU32 colour) {
    if (text.empty() || clip.z <= clip.x) return;
    const bool spaced = std::isspace(static_cast<unsigned char>(text.front())) ||
        std::isspace(static_cast<unsigned char>(text.back()));
    const float gap = spaced ? 0.0f : ImGui::CalcTextSize(" ").x;
    const float repeatWidth = std::max(clip.z - clip.x, ImGui::CalcTextSize(text.c_str()).x + gap);
    auto* drawList = ImGui::GetWindowDrawList();
    const float start = clip.x + wrappingTextOffset(elapsed, repeatWidth, false);
    for (float x = start; x < clip.z; x += repeatWidth) {
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, y), colour,
            text.c_str(), nullptr, 0.0f, &clip);
    }
}

void drawDirectoryTitle(State& state, float width) {
    const std::string& title = state.preview.title;
    constexpr float closeSize = 22.0f;
    const float padding = ImGui::GetStyle().WindowPadding.x;
#ifdef __APPLE__
    const float titleStart = state.onLeft ? 80.0f : padding;
#else
    const float titleStart = padding;
#endif
    const float titleEnd = width - closeSize - padding * 2.0f;
    const ImVec2 position = ImGui::GetWindowPos();
    auto* drawList = ImGui::GetWindowDrawList();
    const ImVec4 titleClip(position.x + titleStart, position.y + 9.0f,
        position.x + titleEnd, position.y + 9.0f + ImGui::GetFontSize());
    drawList->PushClipRect(ImVec2(titleClip.x, titleClip.y), ImVec2(titleClip.z, titleClip.w), true);
    const int firstTextVertex = drawList->VtxBuffer.Size;
    const auto drawTitle = [&](const std::string& text, float alpha, float elapsed) {
        if (text.empty() || alpha <= 0.0f) return;
        ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        colour.w *= alpha;
        drawWrappingText(text, elapsed, titleClip, position.y + 9.0f, ImGui::ColorConvertFloat4ToU32(colour));
    };
    const float blend = state.preview.titleBlend * state.preview.titleBlend * (3.0f - 2.0f * state.preview.titleBlend);
    drawTitle(state.preview.previousTitle, 1.0f - blend, state.previousTitleElapsed);
    drawTitle(title, blend, state.titleElapsed);
    const float fadeWidth = std::max(1.0f, std::min(12.0f, (titleEnd - titleStart) * 0.5f));
    for (int index = firstTextVertex; index < drawList->VtxBuffer.Size; ++index) {
        auto& vertex = drawList->VtxBuffer[index];
        const float distance = std::min(vertex.pos.x - position.x - titleStart, position.x + titleEnd - vertex.pos.x);
        const float edge = std::clamp(distance / fadeWidth, 0.0f, 1.0f);
        const float fade = edge * edge * (3.0f - 2.0f * edge);
        const auto alpha = static_cast<ImU32>(std::round(static_cast<float>(vertex.col >> IM_COL32_A_SHIFT) * fade));
        vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
    }
    drawList->PopClipRect();

    ImGui::SetCursorPos(ImVec2(width - closeSize - 6.0f, 6.0f));
    const ImVec2 closePosition = ImGui::GetCursorScreenPos();
    float radius = ImGui::GetStyle().FrameRounding;
#ifdef __APPLE__
    if (!state.onLeft) {
        const auto* viewport = ImGui::GetWindowViewport();
        if (const auto outer = Renderer::Styling::windowTopCornerRadius(static_cast<GLFWwindow*>(viewport->PlatformHandle), false)) {
            radius = std::clamp(*outer - 6.0f, 0.0f, closeSize * 0.5f);
        }
    }
#endif
    if (ImGui::InvisibleButton("CloseBrowser", ImVec2(closeSize, closeSize))) state.isOpen = false;
    const ImGuiCol colour = ImGui::IsItemActive() ? ImGuiCol_ButtonActive :
        ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button;
    drawList->AddRectFilled(closePosition, ImVec2(closePosition.x + closeSize, closePosition.y + closeSize),
        ImGui::GetColorU32(colour), radius, ImDrawFlags_RoundCornersTopRight);
    const float iconSize = ImGui::GetFontSize() * 0.8f;
    auto* font = ImGui::GetFont()->GetFontBaked(iconSize);
    const auto* glyph = font->FindGlyph(0xf00d);
    const float scale = iconSize / font->Size;
    drawList->PushDrawFlag(ImDrawFlags_TextNoPixelSnap, true);
    drawList->AddText(ImGui::GetFont(), iconSize,
        ImVec2(closePosition.x + (closeSize - (glyph->X0 + glyph->X1) * scale) * 0.5f,
               closePosition.y + (closeSize - (glyph->Y0 + glyph->Y1) * scale) * 0.5f),
        ImGui::GetColorU32(ImGuiCol_Text), ICON_FA_XMARK);
    drawList->PopDrawFlag();
    ImGui::SetCursorPos(ImVec2(6.0f, 9.0f + ImGui::GetTextLineHeight() + 8.0f));
}

}

State::State() = default;
State::~State() = default;

float visibleWidth(const State& state) {
    return std::round(state.resizing.width * state.panelAnimation.value * state.resizeReveal.value);
}

void advancePanel(State& state, float deltaSeconds, int frame) {
    if (state.isOpen && !state.resizing.active && state.resizing.collapsed) {
        state.resizing.collapsed = false;
        if (state.panelAnimation.value == 0.0f) state.resizeReveal.reset(1.0f);
    }
    state.panelAnimation.advance(state.isOpen, deltaSeconds, frame);
    state.resizeReveal.advance(!state.resizing.collapsed, deltaSeconds, frame);
}

void drawResizeHandle(State& state, const ImVec2& displaySize, float sidebarWidth, bool enabled) {
    auto& resizing = state.resizing;
    const bool available = displaySize.x > 0.0f && displaySize.y > 0.0f;
    if (resizing.active && (!enabled || !available || !state.isOpen || resizing.onLeft != state.onLeft)) {
        state.isOpen = state.isOpen && !resizing.collapsed;
        resizing.finish();
    }
    if (!available) return;
    const float layoutWidth = std::max(displaySize.x, state.expandedWindowWidth);
    const float maximumWidth = std::clamp(layoutWidth - sidebarWidth - Resizing::minimumContentWidth,
        Resizing::minimumWidth, Resizing::maximumWidth);
    if (state.isOpen && !resizing.active) {
        resizing.width = std::clamp(resizing.width, Resizing::minimumWidth, maximumWidth);
    }
    const float revealed = visibleWidth(state);
    if (!resizing.active && (!enabled || !state.isOpen || state.panelAnimation.value < 1.0f || state.resizeReveal.value < 1.0f)) return;
    constexpr float gripWidth = 8.0f;
    const float edge = state.onLeft ? revealed : displaySize.x - revealed;
    ImGui::SetNextWindowPos(ImVec2(edge - gripWidth * 0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(gripWidth, displaySize.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##ExplorerResize", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::InvisibleButton("ResizeExplorer", ImVec2(gripWidth, displaySize.y));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated()) resizing.begin(ImGui::GetIO().MousePos.x, state.onLeft);
    if (resizing.active) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            resizing.cancel();
            state.resizeReveal.reset(1.0f);
            ImGui::ClearActiveID();
        } else if (!ImGui::GetIO().MouseDown[ImGuiMouseButton_Left]) {
            state.isOpen = !resizing.collapsed;
            resizing.finish();
        } else {
            const bool feedback = resizing.drag(ImGui::GetIO().MousePos.x, maximumWidth);
#ifdef __APPLE__
            if (feedback) Renderer::Styling::performAlignmentFeedback();
#else
            static_cast<void>(feedback);
#endif
        }
    }
    if (hovered || resizing.active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        const float pixel = 1.0f / std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
        const float border = state.onLeft ? visibleWidth(state) - pixel : displaySize.x - visibleWidth(state);
        ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(border, 0.0f), ImVec2(border + pixel, displaySize.y),
            ImGui::GetColorU32(resizing.snapped ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered));
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void selectDirectory(State& state, const std::string& path) {
    std::error_code error;
    auto root = std::filesystem::weakly_canonical(filesystemPath(path), error);
    if (!error && root.has_relative_path() && root.filename().empty()) root = root.parent_path();
    state.locations.clear();
    state.navigationSequence = 0;
    state.scrolling.reset();
    state.restoreScroll = 0.0f;
    state.restoreLocation = true;
    state.rootDirectory = error ? path : utf8(root);
    state.directory = state.rootDirectory;
    state.preview.browse(state.directory);
    state.isOpen = true;
    state.selectedPath.clear();
    state.requestedFile.clear();
    state.filter.Clear();
    state.titleElapsed = 0.0f;
    state.searchFocus = false;
    state.searchOpen = false;
    refresh(state);
}

void navigate(State& state, const std::string& path, bool previewDirectory) {
    const auto target = filesystemPath(path).lexically_normal();
    const auto relative = target.lexically_relative(filesystemPath(state.rootDirectory));
    if (relative.empty() || *relative.begin() == "..") return;
    if (!state.scanning) {
        state.locations[state.directory] = {state.scrolling.position, state.selectedPath, state.filter.InputBuf, ++state.navigationSequence};
        if (state.locations.size() > 512) {
            const auto oldest = std::min_element(state.locations.begin(), state.locations.end(),
                [](const auto& left, const auto& right) { return left.second.lastVisited < right.second.lastVisited; });
            state.locations.erase(oldest);
        }
    }
    state.directory = utf8(target);
    const auto previous = state.locations.find(state.directory);
    const DirectoryLocation location = previous == state.locations.end() ? DirectoryLocation{} : previous->second;
    state.scrolling.reset();
    state.restoreScroll = location.scroll;
    state.restoreLocation = true;
    state.preview.browse(state.directory, previewDirectory);
    state.selectedPath = location.selection;
    ImStrncpy(state.filter.InputBuf, location.filter.c_str(), IM_ARRAYSIZE(state.filter.InputBuf));
    state.filter.Build();
    state.titleElapsed = 0.0f;
    state.searchFocus = false;
    state.searchOpen = !location.filter.empty();
    refresh(state);
}

void refresh(State& state) {
    if (!state.restoreLocation) {
        state.restoreScroll = state.scrolling.position;
        state.restoreLocation = true;
    }
    state.artworkDirectory.clear();
    if (!state.scanner) state.scanner = std::make_unique<DirectoryScanner>();
    state.scanner->request(state.directory);
    state.files.clear();
    state.visibleFiles.clear();
    state.error.clear();
    state.truncated = false;
    state.scanning = true;
    state.filterDirty = true;
}

void update(State& state, float deltaSeconds) {
    const float delta = std::clamp(deltaSeconds, 0.0f, 0.1f);
    state.hoverElapsed += delta;
    if (state.animatedSelection != state.selectedPath) {
        state.animatedSelection = state.selectedPath;
        state.selectionElapsed = 0.0f;
    } else {
        state.selectionElapsed += delta;
    }
    if (state.isOpen) {
        state.titleElapsed += delta;
        state.previousTitleElapsed += delta;
    }
    if (state.artwork) state.artwork->advance(delta);
    const float targetHeight = state.artwork ? state.artwork->heightRatio() : 0.0f;
    const float blend = 1.0f - std::exp(-delta / 0.08f);
    state.artworkHeight += (targetHeight - state.artworkHeight) * blend;
    if (std::abs(state.artworkHeight - targetHeight) < 0.0005f) state.artworkHeight = targetHeight;
    state.searchAnimation = std::clamp(state.searchAnimation + (state.searchOpen ? delta : -delta) / 0.24f, 0.0f, 1.0f);
    if (state.scanner) {
        if (auto result = state.scanner->takeResult()) {
            state.files = std::move(result->files);
            state.error = std::move(result->error);
            state.truncated = result->truncated;
            state.scanning = false;
            if (!state.selectedPath.empty()) {
                const auto selected = std::find_if(state.files.begin(), state.files.end(),
                    [&](const Entry& entry) { return entry.path == state.selectedPath; });
                state.selectedDirectory = selected != state.files.end() && selected->directory;
                if (selected == state.files.end()) state.selectedPath.clear();
            }
            state.filterDirty = true;
        }
    }
    if (state.filterDirty) rebuildFilter(state);
}

void drawToolbarButton(State& state, float height) {
    if (state.rootDirectory.empty()) return;
    ImGui::SameLine(0.0f, ReSyne::UI::Utilities::kToolbarButtonSpacing);
    if (ReSyne::UI::Utilities::drawToolButton(ICON_FA_FOLDER_OPEN,
            state.isOpen ? "Close Explorer" : "Open Explorer", state.isOpen, true, height)) {
        state.isOpen = !state.isOpen;
    }
}

bool canLoadFiles(UIState& state) {
    const auto& recorder = state.resyneState.recorderState;
    return !state.nativeMenuTracking && !state.helpState.showKeyboardShortcuts && !recorder.isRecording &&
        !recorder.recordingCountdownActive && !recorder.exportRunning.load(std::memory_order_acquire) &&
        !recorder.showExportingDialog && !recorder.showExportDialog && !recorder.showClearTimelineDialog &&
        !recorder.shouldOpenLoadDialog && !recorder.shouldOpenSaveDialog &&
        (ImGui::GetCurrentContext() == nullptr || ImGui::GetTopMostPopupModal() == nullptr);
}

void uploadArtwork(State& state) {
    if (state.artwork) state.artwork->upload();
}

void releaseArtwork(State& state) {
    if (state.artwork) state.artwork->release();
}

namespace {

void drawSearch(State& state) {
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SameLine(0.0f, 6.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float available = ImGui::GetContentRegionAvail().x;
    const float ease = std::log1p(state.searchAnimation * 9.0f) / std::log(10.0f);
    const float width = 22.0f + std::max(0.0f, available - 22.0f) * ease;
    ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + 22.0f),
        ImGui::GetColorU32(ImGuiCol_Button), ImGui::GetStyle().FrameRounding);
    if (ReSyne::UI::Utilities::drawToolButton(state.searchOpen ? ICON_FA_XMARK : ICON_FA_MAGNIFYING_GLASS,
            nullptr, false, true, 22.0f)) {
        state.searchOpen = !state.searchOpen;
        state.searchFocus = state.searchOpen;
        if (!state.searchOpen) {
            state.filter.Clear();
            state.filterDirty = true;
        }
    }
    if (ease < 1.0f) {
        ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        colour.w *= 1.0f - ease;
        const std::string count = state.scanning ? "Reading..." : std::to_string(state.files.size()) + " items";
        ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 28.0f, origin.y + 3.5f),
            ImGui::ColorConvertFloat4ToU32(colour), count.c_str());
    }
    if (width > 32.0f) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 22.0f, origin.y));
        ImGui::SetNextItemWidth(width - 22.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 3.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (state.isOpen && state.searchFocus && state.searchAnimation > 0.4f) {
            ImGui::SetKeyboardFocusHere();
            state.searchFocus = false;
        }
        const bool searching = state.searchOpen && ImGui::GetActiveID() == ImGui::GetID("##FilterFiles");
        const bool closeSearch = state.searchOpen &&
            ((ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ||
             (searching && state.filter.InputBuf[0] == '\0' && ImGui::IsKeyPressed(ImGuiKey_Backspace, false)));
        ImGui::BeginDisabled(!state.searchOpen);
        if (state.filter.DrawWithHint("##FilterFiles", "Filter...")) state.filterDirty = true;
        ImGui::EndDisabled();
        if (closeSearch) {
            state.searchOpen = state.searchFocus = false;
            state.filter.Clear();
            state.filterDirty = true;
            ImGui::ClearActiveID();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + 6.0f, origin.y + 30.0f));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

std::string playingBranch(const State& state) {
    if (state.playingPath.empty()) return {};
    const auto parent = filesystemPath(state.playingPath).parent_path();
    const auto relative = parent.lexically_relative(filesystemPath(state.directory));
    if (relative.empty() || *relative.begin() == ".." || *relative.begin() == ".") return {};
    return utf8(filesystemPath(state.directory) / *relative.begin());
}

bool drawEntry(State& state, const Entry& entry, bool canLoad, bool playing, const char* id) {
    ImGui::PushID(id);
    ImGui::BeginDisabled(!entry.directory && !canLoad);
    const float inset = std::max(0.0f, state.entryPadding - ImGui::GetStyle().WindowPadding.x);
    const ImVec2 rowPosition = ImGui::GetCursorScreenPos();
    const ImVec2 position(rowPosition.x + inset, rowPosition.y);
    const float rowHeight = ImGui::GetTextLineHeight() + 5.0f;
    bool activated = false;
    if (ImGui::Selectable("##Entry", state.selectedPath == entry.path,
            ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SelectOnNav,
            ImVec2(0.0f, rowHeight))) {
        state.selectedPath = entry.path;
        state.selectedDirectory = entry.directory;
        state.preview.select(entry.directory ? entry.path : utf8(filesystemPath(entry.path).parent_path()));
        activated = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) ||
            (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter));
        if (ImGui::IsItemFocused() && !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto* window = ImGui::GetCurrentWindow();
            const float top = rowPosition.y - window->InnerRect.Min.y;
            const float bottom = top + rowHeight - window->InnerRect.GetHeight();
            if (top < 0.0f) state.scrolling.target = std::max(0.0f, state.scrolling.position + top);
            else if (bottom > 0.0f) state.scrolling.target = state.scrolling.position + bottom;
        }
    }
    const bool hovered = ImGui::IsItemHovered();
    const float y = position.y + (rowHeight - ImGui::GetFontSize()) * 0.5f;
    float indent = 0.0f;
    if (playing) {
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(position.x + 3.0f, position.y + rowHeight * 0.5f),
            2.5f, ImGui::ColorConvertFloat4ToU32(state.playingColour));
        indent = 12.0f;
    }
    const float textLeft = position.x + indent;
    const float textRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - 6.0f;
    const float textWidth = ImGui::CalcTextSize(entry.name.c_str()).x;
    const bool selected = state.selectedPath == entry.path;
    const bool scrolling = (hovered || selected) && textWidth > textRight - textLeft;
    float elapsed = 0.0f;
    if (scrolling) {
        if (selected) {
            if (state.animatedSelection != entry.path) {
                state.animatedSelection = entry.path;
                state.selectionElapsed = state.hoveredEntry == std::string(id) + entry.path ? state.hoverElapsed : 0.0f;
            }
            elapsed = state.selectionElapsed;
        } else {
            const std::string hoverKey = std::string(id) + entry.path;
            state.entryHovered = true;
            if (state.hoveredEntry != hoverKey) {
                state.hoveredEntry = hoverKey;
                state.hoverElapsed = 0.0f;
            }
            elapsed = state.hoverElapsed;
        }
    }
    auto* drawList = ImGui::GetWindowDrawList();
    const ImVec4 clip(textLeft, position.y, textRight, position.y + rowHeight);
    drawList->PushClipRect(ImVec2(clip.x, clip.y), ImVec2(clip.z, clip.w), true);
    if (scrolling) drawWrappingText(entry.name, std::max(0.0f, elapsed - 0.65f), clip, y, ImGui::GetColorU32(ImGuiCol_Text));
    else drawList->AddText(ImVec2(textLeft, y), ImGui::GetColorU32(ImGuiCol_Text), entry.name.c_str());
    drawList->PopClipRect();
    ImGui::EndDisabled();
    ImGui::PopID();
    return activated;
}

void prepareScrolling(State& state, Input::Scrolling& scrolling, const ImVec2& origin, const ImVec2& size, float rowHeight) {
    auto& scroll = state.scrolling;
    const float contentHeight = std::max(0.0f, static_cast<float>(state.visibleFiles.size()) * rowHeight - 1.0f);
    const float maximum = std::max(0.0f, contentHeight - size.y);
    if (state.restoreLocation && !state.scanning) {
        scroll.reset(std::clamp(state.restoreScroll, 0.0f, maximum));
        state.restoreLocation = false;
    }
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        ImGui::IsMouseHoveringRect(origin, ImVec2(origin.x + size.x, origin.y + size.y));
    scrolling.advanceRegion(scroll, ImGui::GetID("Files"), hovered, rowHeight, maximum,
        state.isOpen && ImGui::IsWindowContentHoverable(ImGui::GetCurrentWindow()));
    ImGui::SetNextWindowContentSize(ImVec2(0.0f, contentHeight));
    ImGui::SetNextWindowScroll(ImVec2(0.0f, scroll.position));
}

void scrollbar(State& state, bool draw) {
    auto& scroll = state.scrolling;
    auto* window = ImGui::GetCurrentWindow();
    const float maximum = ImGui::GetScrollMaxY();
    if (maximum <= 0.0f) {
        scroll.dragging = scroll.thumbHovered = false;
        return;
    }
    if (scroll.opacity < 0.01f && !scroll.dragging) return;
    const ImRect area(ImVec2(window->InnerRect.Max.x - 10.0f, window->InnerRect.Min.y), window->InnerRect.Max);
    const float height = area.GetHeight();
    const float thumbHeight = std::clamp(height * height / (height + maximum), std::min(24.0f, height), height);
    const float travel = height - thumbHeight;
    const float top = area.Min.y + (travel > 0.0f ? scroll.position / maximum * travel : 0.0f);
    const ImRect thumb(ImVec2(area.Min.x, top), ImVec2(area.Max.x, top + thumbHeight));
    if (draw) {
        const ImGuiCol colour = scroll.dragging ? ImGuiCol_ScrollbarGrabActive :
            scroll.thumbHovered ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab;
        window->DrawList->AddRectFilled(ImVec2(area.Max.x - 4.0f, top), ImVec2(area.Max.x, top + thumbHeight),
            ImGui::GetColorU32(colour, scroll.opacity), 2.0f);
        return;
    }
    const ImGuiID id = window->GetID("OverlayScrollbar");
    ImGui::KeepAliveID(id);
    bool hovered = false;
    bool held = false;
    ImGui::ButtonBehavior(area, id, &hovered, &held);
    if (held && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        scroll.dragOffset = thumb.Contains(ImGui::GetIO().MousePos) ? ImGui::GetIO().MousePos.y - top : thumbHeight * 0.5f;
    }
    scroll.dragging = held;
    scroll.thumbHovered = hovered;
    if (held && travel > 0.0f) {
        scroll.position = scroll.target = std::clamp((ImGui::GetIO().MousePos.y - area.Min.y - scroll.dragOffset) / travel, 0.0f, 1.0f) * maximum;
        scroll.idleSeconds = 0.0f;
    }

}

float artworkSpace(State& state, const ImVec2& displaySize) {
    const auto& directory = state.preview.artworkDirectory;
    if (directory != state.artworkDirectory) {
        state.artworkDirectory = directory;
        if (!state.artwork) state.artwork = std::make_unique<Artwork>();
        state.artwork->request(directory);
    }
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    const float width = ImGui::GetWindowSize().x - padding.x * 2.0f;
    const float height = std::min(width * state.artworkHeight, displaySize.y * 0.65f);
    return height > 0.0f ? height + padding.y : 0.0f;
}

void drawArtwork(State& state, float space, const ImVec2& displaySize) {
    if (space == 0.0f || !state.artwork) return;
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    const float width = ImGui::GetWindowSize().x - padding.x * 2.0f;
    const float height = space - padding.y;
    const ImVec2 min(ImGui::GetWindowPos().x + padding.x, displaySize.y - padding.y - height);
    const ImVec2 max(min.x + width, min.y + height);
    auto* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_WindowBg));
    float radius = 0.0f;
#ifdef __APPLE__
    const auto* viewport = ImGui::GetWindowViewport();
    if (const auto outer = Renderer::Styling::windowBottomCornerRadius(static_cast<GLFWwindow*>(viewport->PlatformHandle), state.onLeft)) {
        radius = std::max(0.0f, *outer - std::max(padding.x, padding.y));
    }
#endif
    drawList->PushClipRect(min, max, true);
    const auto drawImage = [&](bgfx::TextureHandle texture, int imageWidth, int imageHeight, float alpha) {
        if (!bgfx::isValid(texture) || imageWidth <= 0 || imageHeight <= 0 || alpha <= 0.0f) return;
        const float naturalHeight = width * static_cast<float>(imageHeight) / static_cast<float>(imageWidth);
        const float renderedHeight = std::min(displaySize.y * 0.65f, naturalHeight);
        const float renderedWidth = renderedHeight * static_cast<float>(imageWidth) / static_cast<float>(imageHeight);
        const float offset = std::max(0.0f, renderedHeight - height);
        const ImVec2 imageMin(min.x + (width - renderedWidth) * 0.5f, max.y - renderedHeight + offset);
        const ImVec2 imageMax(imageMin.x + renderedWidth, max.y + offset);
        drawList->AddImageRounded(static_cast<ImTextureID>(texture.idx), imageMin, imageMax,
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)), radius,
            state.onLeft ? ImDrawFlags_RoundCornersBottomLeft : ImDrawFlags_RoundCornersBottomRight);
    };
    const float blend = state.artwork->blend * state.artwork->blend * (3.0f - 2.0f * state.artwork->blend);
    const bool incoming = bgfx::isValid(state.artwork->texture);
    drawImage(state.artwork->previousTexture, state.artwork->previousWidth, state.artwork->previousHeight, incoming ? 1.0f : 1.0f - blend);
    drawImage(state.artwork->texture, state.artwork->width, state.artwork->height, blend);
    drawList->PopClipRect();
}

}

void draw(State& state, bool canLoad, const ImVec2& displaySize, Input::Scrolling& scrolling) {
    const float width = std::min(state.resizing.width, displaySize.x);
    const float revealed = std::min(visibleWidth(state), width);
    if (revealed <= 0.0f) return;
    ImGui::SetNextWindowPos(ImVec2(state.onLeft ? revealed - width : displaySize.x - revealed, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, displaySize.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
    ImGui::Begin("File Browser", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        (state.isOpen ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoInputs));

    if (state.preview.browsingDirectory != state.directory) state.preview.browse(state.directory);
    const std::string oldTitle = state.preview.title;
    state.preview.update(state.playingPath, state.trackAvailable, ImGui::GetIO().DeltaTime);
    if (oldTitle != state.preview.title) {
        state.previousTitleElapsed = state.titleElapsed;
        state.titleElapsed = 0.0f;
    }
    state.entryHovered = false;
    drawDirectoryTitle(state, width);
    const bool atRoot = state.directory == state.rootDirectory;
    std::string nextDirectory;
    bool previewDirectory = true;
    if (ReSyne::UI::Utilities::drawToolButton(ICON_FA_CHEVRON_LEFT, "Parent directory", false, !atRoot, 22.0f)) {
        nextDirectory = utf8(filesystemPath(state.directory).parent_path());
        previewDirectory = false;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ReSyne::UI::Utilities::drawToolButton(ICON_FA_REPEAT, "Refresh directory", false, !state.scanning, 22.0f)) refresh(state);
    drawSearch(state);
    if (state.filterDirty) rebuildFilter(state);
    if (!state.error.empty()) ImGui::TextWrapped("%s", state.error.c_str());
    if (state.truncated) ImGui::TextWrapped("Showing %zu items (directory limit). Open a subfolder or load a file directly.", state.files.size());

    const float coverSpace = artworkSpace(state, displaySize);
    const auto branch = playingBranch(state);
    if (!branch.empty()) {
        const Entry pinned{branch, utf8(filesystemPath(branch).filename()), {}, true};
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 1.0f));
        if (drawEntry(state, pinned, true, true, "PlayingFolder")) nextDirectory = branch;
        const ImVec2 separator = ImGui::GetCursorScreenPos();
        const float pixel = 1.0f / std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(separator.x, separator.y + 1.0f),
            ImVec2(separator.x + ImGui::GetContentRegionAvail().x, separator.y + 1.0f + pixel),
            ImGui::GetColorU32(ImGuiCol_Separator, 0.5f));
        ImGui::Dummy(ImVec2(0.0f, 2.0f + pixel));
        ImGui::PopStyleVar();
    }
    const float listHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - coverSpace);
    const float rowStride = ImGui::GetTextLineHeight() + Input::ScrollMotion::rowPadding;
    prepareScrolling(state, scrolling, ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetContentRegionAvail().x, listHeight), rowStride);
    if (ImGui::BeginChild("Files", ImVec2(0.0f, listHeight), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            (state.isOpen ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoInputs))) {
        scrollbar(state, false);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 1.0f));
        const float rowHeight = ImGui::GetTextLineHeight() + 5.0f;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state.visibleFiles.size()), rowHeight + 1.0f);
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const int fileIndex = state.visibleFiles[static_cast<size_t>(index)];
                const auto& file = state.files[static_cast<size_t>(fileIndex)];
                ImGui::PushID(fileIndex);
                if (drawEntry(state, file, canLoad, file.path == branch || file.path == state.playingPath, "ListEntry")) {
                    if (file.directory) nextDirectory = file.path;
                    else state.requestedFile = file.path;
                }
                ImGui::PopID();
            }
        }
        if (state.visibleFiles.empty() && state.error.empty() && !state.scanning) {
            ImGui::TextDisabled("%s", state.files.empty() ? "No supported files or folders" : "No matching files or folders");
        }
        ImGui::PopStyleVar();
        scrollbar(state, true);
    }
    ImGui::EndChild();
    if (!state.entryHovered) {
        state.hoveredEntry.clear();
        state.hoverElapsed = 0.0f;
    }
    drawArtwork(state, coverSpace, displaySize);

    const float scale = std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
    const float dividerWidth = 1.0f / scale;
    const ImVec2 position = ImGui::GetWindowPos();
    const float edge = state.onLeft ? position.x + width : position.x;
    const float dividerX = std::round(edge * scale) / scale - (state.onLeft ? dividerWidth : 0.0f);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(dividerX, 0.0f),
        ImVec2(dividerX + dividerWidth, displaySize.y), ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::End();
    ImGui::PopStyleVar(4);
    if (!nextDirectory.empty()) navigate(state, nextDirectory, previewDirectory);
}

}

#include "ui/file_browser/file_browser.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "ui/file_browser/directory_scanner.h"
#include "ui/file_browser/artwork.h"
#include "ui/ui.h"
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

void drawDirectoryTitle(State& state, float width) {
    const std::string& title = state.preview.title;
    constexpr float closeSize = 22.0f;
#ifdef __APPLE__
    const float titleStart = state.onLeft ? 80.0f : 12.0f;
#else
    const float titleStart = 12.0f;
#endif
    const float titleEnd = width - closeSize - 12.0f;
    const ImVec2 position = ImGui::GetWindowPos();
    auto* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(ImVec2(position.x + titleStart, position.y + 9.0f),
        ImVec2(position.x + titleEnd, position.y + 9.0f + ImGui::GetFontSize()), true);
    const auto drawTitle = [&](const std::string& text, float alpha, float elapsed) {
        if (text.empty() || alpha <= 0.0f) return;
        const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
        const float travel = titleEnd - titleStart + textWidth;
        const float distance = std::fmod(elapsed * 34.0f, travel + 36.0f);
        const float x = state.onLeft ? titleStart - textWidth + distance : titleEnd - distance;
        ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        colour.w *= alpha;
        drawList->AddText(ImVec2(position.x + x, position.y + 9.0f), ImGui::ColorConvertFloat4ToU32(colour), text.c_str());
    };
    const float blend = state.preview.titleBlend * state.preview.titleBlend * (3.0f - 2.0f * state.preview.titleBlend);
    drawTitle(state.preview.previousTitle, 1.0f - blend, state.previousTitleElapsed);
    drawTitle(title, blend, state.titleElapsed);
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

void selectDirectory(State& state, const std::string& path) {
    std::error_code error;
    auto root = std::filesystem::weakly_canonical(filesystemPath(path), error);
    if (!error && root.has_relative_path() && root.filename().empty()) root = root.parent_path();
    state.rootDirectory = error ? path : utf8(root);
    state.directory = state.rootDirectory;
    state.preview.browse(state.directory);
    state.isOpen = true;
    state.selectedPath.clear();
    state.requestedFile.clear();
    state.filter.Clear();
    state.titleElapsed = 0.0f;
    state.searchOpen = state.searchHadText = false;
    refresh(state);
}

void navigate(State& state, const std::string& path, bool previewDirectory) {
    const auto target = filesystemPath(path).lexically_normal();
    const auto relative = target.lexically_relative(filesystemPath(state.rootDirectory));
    if (relative.empty() || *relative.begin() == "..") return;
    state.directory = utf8(target);
    state.preview.browse(state.directory, previewDirectory);
    state.selectedPath.clear();
    state.filter.Clear();
    state.titleElapsed = 0.0f;
    state.searchOpen = state.searchHadText = false;
    refresh(state);
}

void refresh(State& state) {
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
            state.filterDirty = true;
        }
    }
    if (state.filterDirty) rebuildFilter(state);
}

void drawToolbarButton(State& state, float height) {
    if (state.rootDirectory.empty()) return;
    ImGui::SameLine(0.0f, ReSyne::UI::Utilities::kToolbarButtonSpacing);
    if (ReSyne::UI::Utilities::drawToolButton(ICON_FA_FOLDER_OPEN,
            state.isOpen ? "Close file browser" : "Open file browser", state.isOpen, true, height)) {
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
            state.searchHadText = false;
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
        if (state.searchFocus && state.searchAnimation > 0.4f) {
            ImGui::SetKeyboardFocusHere();
            state.searchFocus = false;
        }
        ImGui::BeginDisabled(!state.searchOpen);
        if (state.filter.DrawWithHint("##FilterFiles", "Filter...")) {
            state.filterDirty = true;
            if (state.filter.InputBuf[0] != '\0') state.searchHadText = true;
            else if (state.searchHadText) {
                state.searchOpen = state.searchHadText = false;
                ImGui::ClearActiveID();
            }
        }
        ImGui::EndDisabled();
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
    }
    const float y = position.y + (rowHeight - ImGui::GetFontSize()) * 0.5f;
    float indent = 0.0f;
    if (playing) {
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(position.x + 3.0f, position.y + rowHeight * 0.5f),
            2.5f, ImGui::ColorConvertFloat4ToU32(state.playingColour));
        indent = 12.0f;
    }
    ImGui::GetWindowDrawList()->AddText(ImVec2(position.x + indent, y), ImGui::GetColorU32(ImGuiCol_Text), entry.name.c_str());
    ImGui::EndDisabled();
    ImGui::PopID();
    return activated;
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

void draw(State& state, bool canLoad, const ImVec2& displaySize) {
    const float width = std::min(panelWidth, displaySize.x);
    ImGui::SetNextWindowPos(ImVec2(state.onLeft ? 0.0f : displaySize.x - width, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, displaySize.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
    ImGui::Begin("File Browser", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (state.preview.browsingDirectory != state.directory) state.preview.browse(state.directory);
    const std::string oldTitle = state.preview.title;
    state.preview.update(state.playingPath, state.trackAvailable, ImGui::GetIO().DeltaTime);
    if (oldTitle != state.preview.title) {
        state.previousTitleElapsed = state.titleElapsed;
        state.titleElapsed = 0.0f;
    }
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
    if (ImGui::BeginChild("Files", ImVec2(0.0f, listHeight))) {
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
    }
    ImGui::EndChild();
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

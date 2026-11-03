#include "resyne/ui/toolbar/toolbar.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "resyne/recorder/recorder.h"
#include "resyne/ui/recorder/timeline_actions.h"
#include "ui/file_browser/file_browser.h"
#include "ui/icons.h"
#include "ui/styling/styling.h"

#include <algorithm>
#include <cmath>
#include <string>

#ifdef __APPLE__
#include "renderer/styling/platform_styling.h"
#endif

namespace ReSyne::UI::Utilities {
namespace {

enum class OuterCorner { None, Left, Right };

ImVec4 recordingColour() {
    return ImGui::GetStyleColorVec4(ImGuiCol_Text).x > 0.5f ? ImVec4(1.0f, 0.35f, 0.38f, 1.0f)
        : ImVec4(0.76f, 0.10f, 0.13f, 1.0f);
}

bool buttonSurface(const char* id, const ImVec2& size, OuterCorner corner) {
#ifdef __APPLE__
    if (corner != OuterCorner::None) {
        const bool left = corner == OuterCorner::Left;
        const auto* viewport = ImGui::GetWindowViewport();
        const auto outerRadius = ::Renderer::Styling::windowBottomCornerRadius(
            static_cast<GLFWwindow*>(viewport->PlatformHandle), left);
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float horizontalPadding = left ? position.x - viewport->Pos.x
            : viewport->Pos.x + viewport->Size.x - position.x - size.x;
        const float padding = std::max(horizontalPadding,
            viewport->Pos.y + viewport->Size.y - position.y - size.y);
        const float radius = outerRadius
            ? std::clamp(*outerRadius - padding, 0.0f, std::min(size.x, size.y) * 0.5f) : 0.0f;
        const float rounding = ImGui::GetStyle().FrameRounding;
        if (radius > rounding) {
            auto* window = ImGui::GetCurrentWindow();
            if (window->SkipItems) return false;
            const ImGuiID buttonId = window->GetID(id);
            const ImRect bounds(position, ImVec2(position.x + size.x, position.y + size.y));
            ImGui::ItemSize(size, ImGui::GetStyle().FramePadding.y);
            if (!ImGui::ItemAdd(bounds, buttonId)) return false;
            bool hovered = false, held = false;
            const bool pressed = ImGui::ButtonBehavior(bounds, buttonId, &hovered, &held);
            const ImU32 colour = ImGui::GetColorU32(held && hovered ? ImGuiCol_ButtonActive
                : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
            const float leftRadius = left ? radius : rounding;
            const float rightRadius = left ? rounding : radius;
            auto* drawList = window->DrawList;
            drawList->PathArcToFast(ImVec2(bounds.Min.x + rounding, bounds.Min.y + rounding), rounding, 6, 9);
            drawList->PathArcToFast(ImVec2(bounds.Max.x - rounding, bounds.Min.y + rounding), rounding, 9, 12);
            drawList->PathArcTo(ImVec2(bounds.Max.x - rightRadius, bounds.Max.y - rightRadius), rightRadius, 0.0f, IM_PI * 0.5f);
            drawList->PathArcTo(ImVec2(bounds.Min.x + leftRadius, bounds.Max.y - leftRadius), leftRadius, IM_PI * 0.5f, IM_PI);
            drawList->PathFillConvex(colour);
            ImGui::RenderNavCursor(bounds, buttonId);
            return pressed;
        }
    }
#else
    static_cast<void>(corner);
#endif
    return ImGui::Button(id, size);
}

bool drawButton(const char* id, const char* label, const char* tooltip, const ImVec2& size,
    bool active, bool enabled, bool destructive, bool icon, bool recording, OuterCorner corner) {
    const float textPadding = ImGui::GetStyle().FramePadding.x;
    const float iconFontSize = std::min(size.y - 8.0f, ImGui::GetFontSize() * 0.8f);
    ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    if (!active) base.w = 0.0f;
    auto hover = ImGui::GetStyleColorVec4(active ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
    auto pressed = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    if (destructive) {
        hover = pressed = recordingColour();
        hover.w = 0.14f;
        pressed.w = 0.24f;
    }
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, pressed);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UIStyler::cornerRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(textPadding, std::max(0.0f, (size.y - ImGui::GetFontSize()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 0.45f);
    const auto& gui = *ImGui::GetCurrentContext();
    const bool inheritedDisabled = (gui.CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    const float statusAlpha = inheritedDisabled ? gui.DisabledAlphaBackup : ImGui::GetStyle().Alpha;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, inheritedDisabled ? statusAlpha * 0.45f : statusAlpha);
    ImGui::BeginDisabled(!enabled);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const bool clicked = buttonSurface(id, size, corner);
    const bool hovered = ImGui::IsItemHovered();
    const auto textColour = destructive && hovered ? recordingColour() : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    auto* drawList = ImGui::GetWindowDrawList();
    if (icon) {
        const float fontSize = iconFontSize;
        unsigned int codepoint = 0;
        ImTextCharFromUtf8(&codepoint, label, nullptr);
        auto* font = ImGui::GetFont()->GetFontBaked(fontSize);
        if (const auto* glyph = font->FindGlyph(static_cast<ImWchar>(codepoint))) {
            const float scale = fontSize / font->Size;
            const ImVec2 textPosition(position.x + (size.x - (glyph->X0 + glyph->X1) * scale) * 0.5f,
                position.y + (size.y - (glyph->Y0 + glyph->Y1) * scale) * 0.5f);
            drawList->PushDrawFlag(ImDrawFlags_TextNoPixelSnap, true);
            drawList->AddText(ImGui::GetFont(), fontSize, textPosition, ImGui::GetColorU32(textColour), label);
            drawList->PopDrawFlag();
        }
    } else {
        const auto textSize = ImGui::CalcTextSize(label);
        drawList->AddText(ImVec2(std::floor(position.x + (size.x - textSize.x) * 0.5f),
            std::floor(position.y + (size.y - textSize.y) * 0.5f)), ImGui::GetColorU32(textColour), label);
    }
    if (active && enabled && !inheritedDisabled && !ImGui::IsItemFocused()) {
        drawList->AddRect(ImVec2(position.x + 0.5f, position.y + 0.5f),
            ImVec2(position.x + size.x - 0.5f, position.y + size.y - 0.5f),
            ImGui::GetColorU32(ImGuiCol_NavCursor, 0.30f), UIStyler::cornerRadius);
    }
    if (recording) {
        auto colour = recordingColour();
        colour.w *= statusAlpha;
        drawList->AddCircleFilled(ImVec2(position.x + textPadding, position.y + size.y * 0.5f),
            3.0f, ImGui::ColorConvertFloat4ToU32(colour));
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(3);
    if (tooltip && tooltip[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

struct RecorderLayout {
    float record, stop, load, exportButton;
    float left, right, transport, transportX, utilitiesX, height;
    bool stacked;
};

RecorderLayout recorderLayout(const RecorderState& state, float width) {
    const float buttonHeight = toolbarButtonHeight();
    const float textPadding = ImGui::GetStyle().FramePadding.x;
    const auto textWidth = [textPadding](const char* label) {
        return std::ceil(ImGui::CalcTextSize(label).x) + textPadding * 2.0f;
    };
    RecorderLayout layout{};
    layout.record = textWidth("Record") + 10.0f;
    layout.stop = textWidth("Stop");
    layout.load = textWidth("Load");
    layout.exportButton = textWidth("Export");
    layout.left = layout.record + kToolbarButtonSpacing + layout.stop +
        kToolbarGroupSpacing * 2.0f + kToolbarSeparatorWidth + layout.load;
    layout.transport = buttonHeight * 3.0f + kToolbarButtonSpacing * 2.0f;
    layout.right = buttonHeight * 6.0f + kToolbarButtonSpacing * 3.0f +
        kToolbarIconGroupSpacing * 6.0f + kToolbarSeparatorWidth * 3.0f + layout.exportButton;
    if (!state.fileBrowser.rootDirectory.empty()) layout.right += buttonHeight + kToolbarButtonSpacing;
    layout.stacked = width < layout.left + layout.transport + layout.right + kToolbarGroupSpacing * 2.0f;
    layout.utilitiesX = std::max(0.0f, width - layout.right);
    layout.transportX = layout.stacked ? std::max(layout.left + kToolbarGroupSpacing, width - layout.transport)
        : std::clamp((width - layout.transport) * 0.5f, layout.left + kToolbarGroupSpacing,
            layout.utilitiesX - kToolbarGroupSpacing - layout.transport);
    layout.height = buttonHeight + kToolbarVerticalPadding * 2.0f;
    if (layout.stacked) layout.height += buttonHeight + kToolbarVerticalPadding;
    return layout;
}

}

bool drawToolButton(const char* label, const char* tooltip, bool active, bool enabled, float height, bool destructive) {
    const std::string id = std::string("##ToolBtn_") + label;
    return drawButton(id.c_str(), label, tooltip, ImVec2(height, height), active, enabled, destructive, true, false, OuterCorner::None);
}

void drawToolbarSeparator(float height, float spacing) {
    ImGui::SameLine(0.0f, spacing);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float inset = height * 0.25f;
    ImGui::Dummy(ImVec2(kToolbarSeparatorWidth, height));
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(position.x, position.y + inset),
        ImVec2(position.x + kToolbarSeparatorWidth, position.y + height - inset),
        ImGui::GetColorU32(ImGuiCol_Separator, 0.55f));
    ImGui::SameLine(0.0f, spacing);
}
ToolbarRenderResult renderToolbar(ToolState& state, const ToolbarRenderContext& context) {
    ToolbarRenderResult result;

    ImGui::BeginGroup();

    const bool enabled = context.enabled;
    const float buttonHeight = context.buttonHeight > 0.0f ? context.buttonHeight : toolbarButtonHeight();

    if (!context.allowGrabTool && state.activeTool == ToolType::Grab) {
        state.activeTool = ToolType::Cursor;
    }

    struct ToolEntry {
        ToolType type;
        bool allowed;
    };

    const ToolEntry tools[] = {
        {ToolType::Cursor, true},
        {ToolType::Zoom, true},
        {ToolType::Grab, context.allowGrabTool}
    };
    bool first = true;
    for (const ToolEntry& toolEntry : tools) {
        const ToolType tool = toolEntry.type;
        if (!first) {
            ImGui::SameLine(0.0f, kToolbarButtonSpacing);
        }
        first = false;

        const char* label = toolLabel(tool);
        const char* tooltip = toolTooltip(tool);

        const bool active = state.activeTool == tool;
        const bool toolEnabled = enabled && toolEntry.allowed;
        if (drawToolButton(label, tooltip, active, toolEnabled, buttonHeight)) {
            state.activeTool = tool;
        }
    }

    ImGui::EndGroup();

    result.size = ImGui::GetItemRectSize();
    return result;
}

float recorderToolbarHeight(const RecorderState& state, float width) {
    return recorderLayout(state, width).height;
}

void renderRecorderToolbar(RecorderState& state, const RecorderToolbarContext& context) {
    const auto layout = recorderLayout(state, context.width);
    const auto origin = ImGui::GetCursorScreenPos();
    const float top = origin.y + kToolbarVerticalPadding;
    const float height = toolbarButtonHeight();
    const bool exporting = state.exportRunning.load(std::memory_order_acquire);
    const bool timelineEnabled = context.enabled && context.hasData;
    const bool transportEnabled = timelineEnabled && !state.isRecording;
    const bool playing = state.audioOutput && state.audioOutput->isPlaying();
    const auto textButton = [&](const char* id, const char* label, const char* tooltip, float width,
        bool enabled, bool active = false, bool recording = false, OuterCorner corner = OuterCorner::None) {
        return drawButton(id, label, tooltip, ImVec2(width, height), active, enabled, false, false, recording, corner);
    };
    ImGui::PushID("Recorder toolbar");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kToolbarButtonSpacing, 0.0f));
    ImGui::BeginDisabled(!context.enabled);
    if (!context.timelineHasBorder) {
        const float thickness = 1.0f / std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
        ImGui::GetWindowDrawList()->AddLine(origin, ImVec2(origin.x + context.width, origin.y),
            ImGui::GetColorU32(ImGuiCol_Separator, 0.55f), thickness);
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, top));
    const bool recordEnabled = context.enabled && !exporting && !state.isRecording && context.canStartRecording;
    if (textButton("##Record", RecorderUI::recordingStartButtonLabel(state),
            context.canStartRecording ? "Record audio" : "Select an input device in the sidebar", layout.record,
            recordEnabled, state.recordingCountdownActive || state.isRecording, state.isRecording,
            context.outerLeftCorner && !layout.stacked ? OuterCorner::Left : OuterCorner::None)) {
        if (state.recordingCountdownActive) RecorderUI::cancelRecordingCountdown(state);
        else RecorderUI::beginRecordingCountdown(state);
    }
    if (context.enabled && !exporting && !context.canStartRecording && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Select an input device in the sidebar");
    ImGui::SameLine(0.0f, kToolbarButtonSpacing);
    if (textButton("##StopRecording", "Stop", "Stop recording", layout.stop, context.enabled && !exporting && state.isRecording))
        Recorder::stopRecording(state);
    drawToolbarSeparator(height);
    if (textButton("##Load", "Load", "Load audio, .rsyn, or TIFF files", layout.load,
            context.enabled && !exporting && !state.isRecording && !state.recordingCountdownActive)) state.shouldOpenLoadDialog = true;

    ImGui::SetCursorScreenPos(ImVec2(origin.x + layout.transportX, top));
    if (drawToolButton(ICON_FA_REPEAT, state.loopEnabled ? "Loop enabled" : "Loop disabled",
            state.loopEnabled, transportEnabled, height)) {
        state.loopEnabled = !state.loopEnabled;
        if (state.audioOutput) state.audioOutput->setLoopEnabled(state.loopEnabled);
    }
    ImGui::SameLine(0.0f, kToolbarButtonSpacing);
    if (drawToolButton(ICON_FA_BACKWARD_STEP, "Jump to start", false, transportEnabled, height)) Recorder::seekPlayback(state, 0.0f);
    ImGui::SameLine(0.0f, kToolbarButtonSpacing);
    if (drawToolButton(playing ? ICON_FA_PAUSE : ICON_FA_PLAY, playing ? "Pause" : "Play", false, transportEnabled, height))
        Recorder::togglePlayback(state);

    ImGui::SetCursorScreenPos(ImVec2(origin.x + layout.utilitiesX,
        top + (layout.stacked ? height + kToolbarVerticalPadding : 0.0f)));
    renderToolbar(state.toolState, ToolbarRenderContext{timelineEnabled, height, context.hasData && !state.timeline.trackScrubber});
    ImGui::SameLine(0.0f, kToolbarButtonSpacing);
    if (drawToolButton(ICON_FA_CROSSHAIRS, state.timeline.trackScrubber ? "Stop following scrubber" : "Follow scrubber",
            state.timeline.trackScrubber, timelineEnabled, height)) {
        state.timeline.trackScrubber = !state.timeline.trackScrubber;
        if (state.timeline.trackScrubber) {
            state.timeline.viewCentreNormalised = state.timeline.scrubberNormalisedPosition;
            state.toolState.activeTool = ToolType::Cursor;
        }
    }
    drawToolbarSeparator(height, kToolbarIconGroupSpacing);
    const bool detachedEnabled = timelineEnabled && state.detachedVisualisation.available && !state.detachedVisualisation.isOpen;
    if (drawToolButton(ICON_FA_WINDOW_RESTORE, nullptr, state.detachedVisualisation.isOpen, detachedEnabled, height))
        state.detachedVisualisation.openRequested = true;
    if (context.enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", !state.detachedVisualisation.available ? kDetachedVisualisationUnsupportedTooltip
            : state.detachedVisualisation.isOpen ? kDetachedVisualisationAlreadyOpenTooltip : kDetachedVisualisationTooltip);
    }
    ::UI::FileBrowser::drawToolbarButton(state.fileBrowser, height);
    drawToolbarSeparator(height, kToolbarIconGroupSpacing);
    const bool clearEnabled = timelineEnabled && !exporting && !state.isRecording && !state.recordingCountdownActive &&
        !state.importRunning.load(std::memory_order_acquire);
    if (drawToolButton(ICON_FA_TRASH, "Clear timeline", false, clearEnabled, height, true)) RecorderUI::requestTimelineClear(state);
    drawToolbarSeparator(height, kToolbarIconGroupSpacing);
    if (textButton("##Export", "Export", "Export recording to file", layout.exportButton,
            timelineEnabled && !exporting && !state.showExportingDialog && !state.isRecording && !state.recordingCountdownActive,
            false, false, context.outerRightCorner ? OuterCorner::Right : OuterCorner::None)) state.showExportDialog = true;
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(context.width, layout.height));
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

}

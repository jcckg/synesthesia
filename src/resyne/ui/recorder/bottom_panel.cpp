#include "resyne/recorder/recorder.h"

#include <algorithm>
#include <vector>
#include <mutex>

#include "audio/analysis/fft/fft_processor.h"
#include "imgui.h"
#include "ui/icons.h"
#include "resyne/ui/recorder/utils.h"
#include "resyne/ui/recorder/recorder_constants.h"
#include "resyne/ui/recorder/shared_components.h"
#include "resyne/ui/toolbar/toolbar.h"

namespace ReSyne {

void Recorder::drawBottomPanel(RecorderState& state,
                               FFTProcessor& fftProcessor,
                               float panelX,
                               float panelY,
                               float panelWidth,
                               float panelHeight) {
    if (!state.windowOpen) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(panelX, panelY));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));

    ImGui::Begin("##ResyneRecorderPanel",
                 nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (state.focusRequested) {
        ImGui::SetWindowFocus();
        state.focusRequested = false;
    }

    const float buttonHeight = 25.0f;
    const float buttonWidth = 80.0f;
    const float controlsRowHeight = buttonHeight * 2 + 32.0f;

    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const ImVec2 gradientSize(ImGui::GetContentRegionAvail().x, availableHeight - controlsRowHeight);

    size_t sampleCount = 0;
    double duration = 0.0;
    bool hasData = false;
    std::vector<ReSyne::Timeline::TimelineSample> previewData;

    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);

        const bool usePreview = state.importPhase == 3 && state.previewReady.load(std::memory_order_acquire);
        const auto& sourceSamples = usePreview ? state.previewSamples : state.samples;

        sampleCount = sourceSamples.size();
        hasData = !sourceSamples.empty() || (!state.previewSamples.empty() && state.importPhase == 3);

        if (hasData) {
            if (!state.reconstructedAudio.empty() && state.metadata.sampleRate > 0.0f) {
                const uint32_t numChannels = state.metadata.channels > 0 ? state.metadata.channels : 1;
                const size_t totalFrames = state.reconstructedAudio.size() / numChannels;
                duration = static_cast<double>(totalFrames) / static_cast<double>(state.metadata.sampleRate);
            } else {
                duration = sourceSamples.back().timestamp;
            }
            previewData = ReSyne::UI::samplePreviewData(state, ReSyne::UI::MAX_PREVIEW_SAMPLES_BOTTOM_PANEL, lock);
        }
    }

    if (hasData) {
        ReSyne::Timeline::RenderContext timelineContext{
            previewData,
            duration,
            gradientSize,
            state.dropFlashAlpha,
            ImGui::GetIO().DeltaTime,
            state.importColourSpace,
            RecorderUI::computePlaybackNormalisedPosition(state),
            true,
            false,
            true,
            !state.timeline.trackScrubber,
            !state.timeline.trackScrubber,
            &state.toolState,
            &state.trackpadInput
        };

        if (!state.timeline.isScrubberDragging && timelineContext.playbackNormalisedPosition.has_value()) {
            state.timeline.scrubberNormalisedPosition = *timelineContext.playbackNormalisedPosition;
        }

        const auto timelineResult = ReSyne::Timeline::renderTimeline(state.timeline, timelineContext);
        RecorderUI::handleTimelineScrub(state, timelineResult);
    } else {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        const ImVec2 regionMax(cursorPos.x + gradientSize.x, cursorPos.y + gradientSize.y);
        state.timeline.gradientRegionMin = cursorPos;
        state.timeline.gradientRegionMax = regionMax;
        state.timeline.gradientRegionValid = gradientSize.x > 1.0f && gradientSize.y > 1.0f;
        drawList->AddRectFilled(cursorPos, regionMax, IM_COL32(30, 30, 30, 255));
        ImGui::Dummy(gradientSize);

        ImGuiIO& ioLocal = ImGui::GetIO();
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
        if (hovered) {
            state.timeline.hoverOverlayAlpha =
                std::min(1.0f, state.timeline.hoverOverlayAlpha + ioLocal.DeltaTime * 6.0f);
        } else {
            state.timeline.hoverOverlayAlpha =
                std::max(0.0f, state.timeline.hoverOverlayAlpha - ioLocal.DeltaTime * 3.0f);
        }

        const float combinedAlpha =
            state.timeline.hoverOverlayAlpha * 0.3f + state.dropFlashAlpha * 0.6f;
        if (combinedAlpha > 0.01f) {
            drawList->AddRectFilled(cursorPos,
                                     regionMax,
                                     ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, combinedAlpha)));
        }
    }

    ImGui::Spacing();

    const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    Recorder::handleKeyboardShortcuts(state, panelFocused, hasData && !state.isRecording);

    const float toolbarButtonSpacing = 4.0f;
    const float transportSpacing = 8.0f;
    const float transportButtonSize = buttonHeight;
    const int toolbarButtonCount = 3;
    const int transportButtonCount = 3;
    const float playbackWidth = transportButtonCount * transportButtonSize +
                                std::max(0, transportButtonCount - 1) * transportSpacing;
    const float toolbarWidth = toolbarButtonCount * transportButtonSize +
                               std::max(0, toolbarButtonCount - 1) * toolbarButtonSpacing;
    const float controlsSpacing = transportSpacing * 1.5f;
    const float lockToExportSpacing = 6.0f;
    const float lockAndExportWidth = transportButtonSize + lockToExportSpacing + buttonWidth;
    const float rightGroupWidth = toolbarWidth + controlsSpacing + lockAndExportWidth;

    if (ImGui::BeginTable("RecorderBottomControls",
                          3,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("Record", ImGuiTableColumnFlags_WidthFixed,
                                buttonWidth * 3.0f + transportSpacing * 3.0f);
        ImGui::TableSetupColumn("Transport", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Utilities", ImGuiTableColumnFlags_WidthFixed, rightGroupWidth + 12.0f);

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginDisabled(state.isRecording);
        if (ImGui::Button("Start", ImVec2(buttonWidth, buttonHeight))) {
            startRecording(state, fftProcessor, 2048, 1024);
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, transportSpacing);

        ImGui::BeginDisabled(!state.isRecording);
        if (ImGui::Button("Stop", ImVec2(buttonWidth, buttonHeight))) {
            stopRecording(state);
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, transportSpacing);

        ImGui::BeginDisabled(state.isRecording);
        if (ImGui::Button("Load", ImVec2(buttonWidth, buttonHeight))) {
            state.shouldOpenLoadDialog = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Load audio, .resyne/.synesthesia, or TIFF files");
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        if (hasData) {
            ImGui::Text("%zu frames  •  %.1fs", sampleCount, duration);
        } else {
            ImGui::TextDisabled("No data recorded");
        }

        ImGui::TableSetColumnIndex(1);
        const float centreColumnWidth = ImGui::GetColumnWidth(1);
        const float centreCursorStart = ImGui::GetCursorPosX();
        const float centreOffset = std::max(0.0f, (centreColumnWidth - playbackWidth) * 0.5f);
        ImGui::SetCursorPosX(centreCursorStart + centreOffset);

        const bool isPlaying = state.audioOutput && state.audioOutput->isPlaying();
        const char* playPauseIcon = isPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY;

        ImGui::BeginDisabled(!hasData || state.isRecording);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              state.loopEnabled
                                  ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                                  : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::Button(ICON_FA_REPEAT, ImVec2(transportButtonSize, transportButtonSize))) {
            state.loopEnabled = !state.loopEnabled;
            if (state.audioOutput) {
                state.audioOutput->setLoopEnabled(state.loopEnabled);
            }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(state.loopEnabled ? "Loop enabled" : "Loop disabled");
        }

        ImGui::SameLine(0.0f, transportSpacing);

        if (ImGui::Button(ICON_FA_BACKWARD_STEP, ImVec2(transportButtonSize, transportButtonSize))) {
            Recorder::seekPlayback(state, 0.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Jump to start");
        }

        ImGui::SameLine(0.0f, transportSpacing);

        if (ImGui::Button(playPauseIcon, ImVec2(transportButtonSize, transportButtonSize))) {
            if (isPlaying) {
                pausePlayback(state);
            } else {
                startPlayback(state);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(isPlaying ? "Pause" : "Play");
        }
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(2);
        const float rightColumnWidth = ImGui::GetColumnWidth(2);
        const float rightCursorStart = ImGui::GetCursorPosX();
        const float rightOffset = std::max(0.0f, rightColumnWidth - rightGroupWidth);
        ImGui::SetCursorPosX(rightCursorStart + rightOffset);

        UI::Utilities::ToolbarRenderContext toolbarContext;
        toolbarContext.enabled = hasData;
        toolbarContext.buttonHeight = transportButtonSize;
        toolbarContext.allowGrabTool = hasData && !state.timeline.trackScrubber;
        UI::Utilities::renderToolbar(state.toolState, toolbarContext);

        ImGui::SameLine(0.0f, controlsSpacing);
        ImGui::BeginDisabled(!hasData);

        const bool trackingActive = state.timeline.trackScrubber;
        const ImVec4 trackBase = trackingActive
                                     ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                                     : ImGui::GetStyleColorVec4(ImGuiCol_Button);
        const ImVec4 trackHover = trackingActive
                                      ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                                      : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        ImGui::PushStyleColor(ImGuiCol_Button, trackBase);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, trackHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(ICON_FA_CROSSHAIRS, ImVec2(transportButtonSize, transportButtonSize))) {
            state.timeline.trackScrubber = !state.timeline.trackScrubber;
            if (state.timeline.trackScrubber) {
                state.timeline.viewCentreNormalised = state.timeline.scrubberNormalisedPosition;
                state.toolState.activeTool = UI::Utilities::ToolType::Cursor;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(state.timeline.trackScrubber ? "Click to stop tracking" : "Click to follow scrubber");
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f, lockToExportSpacing);

        if (ImGui::Button("Export", ImVec2(buttonWidth, buttonHeight))) {
            state.showExportDialog = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Export recording to file");
        }

        ImGui::EndDisabled();

        ImGui::EndTable();
    }

    ReSyne::UI::renderStatusMessage(state);

    ImGui::End();
    ImGui::PopStyleVar();

    drawExportDialog(state);
    drawLoadingDialog(state);
    drawExportingDialog(state);
}

}

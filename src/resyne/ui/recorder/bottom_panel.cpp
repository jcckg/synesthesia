#include "resyne/recorder/recorder.h"

#include <algorithm>
#include <vector>
#include <mutex>

#include "audio/analysis/fft/fft_processor.h"
#include "imgui.h"
#include "ui/icons.h"
#include "ui/input/shortcuts.h"
#include "resyne/ui/recorder/utils.h"
#include "resyne/ui/recorder/recorder_constants.h"
#include "resyne/ui/recorder/shared_components.h"
#include "resyne/recorder/presentation_worker.h"
#include "resyne/encoding/formats/rsyn_presentation.h"
#include "resyne/ui/recorder/timeline_actions.h"
#include "resyne/ui/toolbar/toolbar.h"

namespace ReSyne {

void Recorder::drawBottomPanel(RecorderState& state,
                               AudioProcessor& audioProcessor,
                               bool canStartRecording,
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

    const bool exportRunning = state.exportRunning.load(std::memory_order_acquire);
    const bool controlsEnabled = state.importPhase == 0;
    ImGui::BeginDisabled(!controlsEnabled);

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

    const auto presentation = recorderPresentation(state);
    if (presentation) {
        sampleCount = presentation->sourceFrames;
        hasData = presentation->colours->size() > 0;
        duration = presentation->duration;
    }

    if (hasData) {
        ReSyne::Timeline::RenderContext timelineContext{
            previewData,
            presentation->revision,
            duration,
            gradientSize,
            state.dropFlashAlpha,
            ImGui::GetIO().DeltaTime,
            state.importColourSpace,
            presentation->settings.applyGamutMapping,
            state.importPhase == 0 ? RecorderUI::computePlaybackNormalisedPosition(state) : std::nullopt,
            controlsEnabled,
            false,
            controlsEnabled,
            controlsEnabled && !state.timeline.trackScrubber,
            controlsEnabled && !state.timeline.trackScrubber,
            &state.toolState,
            &state.trackpadInput,
            state.presentationResources,
            presentation->colours.get()
        };

        if (presentation->preview) timelineContext.availableDurationSeconds = presentation->colours->endTime();

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
        bool preparing = state.importPhase != 0;
        {
            std::lock_guard lock(state.samplesMutex);
            preparing = preparing || !state.samples.empty() || state.metadata.presentationData != nullptr;
        }
        if (preparing) {
            const char* message = "Preparing track…";
            const auto textSize = ImGui::CalcTextSize(message);
            drawList->AddText(ImVec2(cursorPos.x + (gradientSize.x - textSize.x) * 0.5f,
                                    cursorPos.y + (gradientSize.y - textSize.y) * 0.5f),
                              IM_COL32(180, 180, 180, 255), message);
        }

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
    if (controlsEnabled) ::UI::Input::handleTimelineShortcuts(state, panelFocused);
    if ((!canStartRecording || !controlsEnabled || exportRunning) && state.recordingCountdownActive) {
        RecorderUI::cancelRecordingCountdown(state);
    }
    if (RecorderUI::updateRecordingCountdown(state, ImGui::GetIO().DeltaTime)) {
        startRecording(state, audioProcessor, 0, 0);
    }

    constexpr float buttonSpacing = UI::Utilities::kToolbarButtonSpacing;
    const float transportButtonSize = buttonHeight;
    const int toolbarButtonCount = 3;
    const int transportButtonCount = 3;
    const float playbackWidth = transportButtonCount * transportButtonSize +
                                std::max(0, transportButtonCount - 1) * buttonSpacing;
    const float toolbarWidth = toolbarButtonCount * transportButtonSize +
                               std::max(0, toolbarButtonCount - 1) * buttonSpacing;
    const float utilityControlsWidth = transportButtonSize * 3.0f + buttonSpacing * 4.0f +
                                      UI::Utilities::kToolbarSeparatorWidth + buttonWidth;
    const float rightGroupWidth = toolbarWidth + buttonSpacing + utilityControlsWidth;

    if (ImGui::BeginTable("RecorderBottomControls",
                          3,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("Record", ImGuiTableColumnFlags_WidthFixed,
                                buttonWidth * 3.0f + buttonSpacing * 2.0f +
                                (state.fileBrowser.directory.empty() ? 0.0f : buttonHeight + buttonSpacing));
        ImGui::TableSetupColumn("Transport", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Utilities", ImGuiTableColumnFlags_WidthFixed, rightGroupWidth);

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginDisabled(exportRunning || state.isRecording || !canStartRecording);
        if (ImGui::Button(RecorderUI::recordingStartButtonLabel(state), ImVec2(buttonWidth, buttonHeight))) {
            if (state.recordingCountdownActive) {
                RecorderUI::cancelRecordingCountdown(state);
            } else {
                RecorderUI::beginRecordingCountdown(state);
            }
        }
        if (controlsEnabled && !exportRunning && !canStartRecording &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Select an input device in the sidebar");
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, buttonSpacing);

        ImGui::BeginDisabled(exportRunning || !state.isRecording);
        if (ImGui::Button("Stop", ImVec2(buttonWidth, buttonHeight))) {
            stopRecording(state);
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, buttonSpacing);

        ImGui::BeginDisabled(exportRunning || state.isRecording || state.recordingCountdownActive);
        if (ImGui::Button("Load", ImVec2(buttonWidth, buttonHeight))) {
            state.shouldOpenLoadDialog = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Load audio, .rsyn, or TIFF files");
        }
        ImGui::EndDisabled();
        ::UI::FileBrowser::drawToolbarButton(state.fileBrowser, buttonHeight);

        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        if (hasData) {
            ImGui::Text("%zu frames  •  %.1fs", sampleCount, duration);
        } else {
            ImGui::TextDisabled("No data recorded");
        }

        ImGui::TableSetColumnIndex(1);
        const float centreColumnWidth = ImGui::GetContentRegionAvail().x;
        const float centreCursorStart = ImGui::GetCursorPosX();
        const float centreOffset = std::max(0.0f, (centreColumnWidth - playbackWidth) * 0.5f);
        ImGui::SetCursorPosX(centreCursorStart + centreOffset);

        const bool isPlaying = state.audioOutput && state.audioOutput->isPlaying();
        const char* playPauseIcon = isPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY;

        const bool transportEnabled = controlsEnabled && hasData && !state.isRecording;
        if (UI::Utilities::drawToolButton(ICON_FA_REPEAT,
                                          state.loopEnabled ? "Loop enabled" : "Loop disabled",
                                          state.loopEnabled,
                                          transportEnabled,
                                          transportButtonSize)) {
            state.loopEnabled = !state.loopEnabled;
            if (state.audioOutput) {
                state.audioOutput->setLoopEnabled(state.loopEnabled);
            }
        }
        ImGui::SameLine(0.0f, buttonSpacing);

        if (UI::Utilities::drawToolButton(ICON_FA_BACKWARD_STEP, "Jump to start", false, transportEnabled, transportButtonSize)) {
            Recorder::seekPlayback(state, 0.0f);
        }

        ImGui::SameLine(0.0f, buttonSpacing);

        if (UI::Utilities::drawToolButton(playPauseIcon, isPlaying ? "Pause" : "Play", false, transportEnabled, transportButtonSize)) {
            togglePlayback(state);
        }

        ImGui::TableSetColumnIndex(2);
        const float rightColumnWidth = ImGui::GetContentRegionAvail().x;
        const float rightCursorStart = ImGui::GetCursorPosX();
        const float rightOffset = std::max(0.0f, rightColumnWidth - rightGroupWidth);
        ImGui::SetCursorPosX(rightCursorStart + rightOffset);

        const bool timelineControlsEnabled = controlsEnabled && hasData;
        UI::Utilities::ToolbarRenderContext toolbarContext;
        toolbarContext.enabled = timelineControlsEnabled;
        toolbarContext.buttonHeight = transportButtonSize;
        toolbarContext.allowGrabTool = hasData && !state.timeline.trackScrubber;
        UI::Utilities::renderToolbar(state.toolState, toolbarContext);

        ImGui::SameLine(0.0f, buttonSpacing);
        ImGui::BeginDisabled(!timelineControlsEnabled);

        const bool trackingActive = state.timeline.trackScrubber;
        if (UI::Utilities::drawToolButton(ICON_FA_CROSSHAIRS,
                                          trackingActive ? "Click to stop tracking" : "Click to follow scrubber",
                                          trackingActive,
                                          timelineControlsEnabled,
                                          transportButtonSize)) {
            state.timeline.trackScrubber = !state.timeline.trackScrubber;
            if (state.timeline.trackScrubber) {
                state.timeline.viewCentreNormalised = state.timeline.scrubberNormalisedPosition;
                state.toolState.activeTool = UI::Utilities::ToolType::Cursor;
            }
        }

        UI::Utilities::drawToolbarSeparator(transportButtonSize);

        const bool detachedVisualisationEnabled =
            timelineControlsEnabled &&
            state.detachedVisualisation.available &&
            !state.detachedVisualisation.isOpen;
        if (UI::Utilities::drawToolButton(
                ICON_FA_WINDOW_RESTORE,
                nullptr,
                state.detachedVisualisation.isOpen,
                detachedVisualisationEnabled,
                transportButtonSize)) {
            state.detachedVisualisation.openRequested = true;
        }
        if (controlsEnabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (!state.detachedVisualisation.available) {
                ImGui::SetTooltip("%s", ReSyne::UI::kDetachedVisualisationUnsupportedTooltip);
            } else if (state.detachedVisualisation.isOpen) {
                ImGui::SetTooltip("%s", ReSyne::UI::kDetachedVisualisationAlreadyOpenTooltip);
            } else {
                ImGui::SetTooltip("%s", ReSyne::UI::kDetachedVisualisationTooltip);
            }
        }

        ImGui::SameLine(0.0f, buttonSpacing);

        const bool clearTimelineEnabled =
            timelineControlsEnabled && !exportRunning && !state.isRecording && !state.recordingCountdownActive &&
            !state.importRunning.load(std::memory_order_acquire);
        if (UI::Utilities::drawToolButton(
                ICON_FA_TRASH,
                "Clear timeline",
                false,
                clearTimelineEnabled,
                transportButtonSize,
                true)) {
            RecorderUI::requestTimelineClear(state);
        }

        ImGui::SameLine(0.0f, buttonSpacing);

        ImGui::BeginDisabled(exportRunning || state.showExportingDialog || state.isRecording || state.recordingCountdownActive);
        if (ImGui::Button("Export", ImVec2(buttonWidth, buttonHeight))) {
            state.showExportDialog = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Export recording to file");
        }
        ImGui::EndDisabled();

        ImGui::EndDisabled();

        ImGui::EndTable();
    }

    ReSyne::UI::renderStatusMessage(state);

    ImGui::EndDisabled();
    ImGui::End();
    ImGui::PopStyleVar();

    drawExportDialog(state);
    RecorderUI::drawClearTimelineDialog(state);
}

}

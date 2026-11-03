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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::kToolbarHorizontalPadding, UI::kToolbarVerticalPadding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(UI::Utilities::kToolbarButtonSpacing, UI::kToolbarVerticalPadding));

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

    const float toolbarWidth = ImGui::GetContentRegionAvail().x;
    const float controlsRowHeight = UI::Utilities::recorderToolbarHeight(state, toolbarWidth) + ImGui::GetTextLineHeight() + UI::kToolbarVerticalPadding;

    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const ImVec2 timelineOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 gradientSize(ImGui::GetContentRegionAvail().x, std::max(1.0f, availableHeight - controlsRowHeight));

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

    ImGui::SetCursorScreenPos(ImVec2(timelineOrigin.x, timelineOrigin.y + gradientSize.y));

    const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (controlsEnabled) ::UI::Input::handleTimelineShortcuts(state, panelFocused);
    if ((!canStartRecording || !controlsEnabled || exportRunning) && state.recordingCountdownActive) {
        RecorderUI::cancelRecordingCountdown(state);
    }
    if (RecorderUI::updateRecordingCountdown(state, ImGui::GetIO().DeltaTime)) {
        startRecording(state, audioProcessor, 0, 0);
    }

    UI::Utilities::RecorderToolbarContext toolbar;
    toolbar.enabled = controlsEnabled;
    toolbar.canStartRecording = canStartRecording;
    toolbar.hasData = hasData;
    toolbar.timelineHasBorder = hasData;
    toolbar.width = toolbarWidth;
    UI::Utilities::renderRecorderToolbar(state, toolbar);
    if (hasData) ImGui::TextDisabled("%zu frames  |  %.1fs", sampleCount, duration);
    else ImGui::TextDisabled("No data recorded");

    ReSyne::UI::renderStatusMessage(state);

    ImGui::EndDisabled();
    ImGui::End();
    ImGui::PopStyleVar(2);

    drawExportDialog(state);
    RecorderUI::drawClearTimelineDialog(state);
}

}

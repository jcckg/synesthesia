#include "resyne/recorder/recorder.h"

#include <algorithm>
#include <vector>
#include <mutex>
#include <limits>

#include "audio/analysis/fft/fft_processor.h"
#include "audio/input/audio_input.h"
#include "imgui.h"
#include "ui/icons.h"
#include "resyne/ui/recorder/utils.h"
#include "resyne/ui/recorder/recorder_constants.h"
#include "resyne/ui/recorder/shared_components.h"
#include "resyne/ui/toolbar/toolbar.h"

namespace ReSyne {

void Recorder::drawFullWindow(RecorderState& state,
                              AudioInput& audioInput,
                              FFTProcessor& fftProcessor,
                              float windowX,
                              float windowY,
                              float windowWidth,
                              float windowHeight,
                              float currentR,
                              float currentG,
                              float currentB) {
    ImGui::SetNextWindowPos(ImVec2(windowX, windowY));
    ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.0f, 9.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

    ImGui::Begin("##ResyneFullWindow",
                 nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);

    constexpr float CONTROL_HEIGHT = 30.0f;
    size_t sampleCount = 0;
    double duration = 0.0;
    bool hasData = false;
    int fftSize = state.metadata.fftSize;
    int hopSize = state.metadata.hopSize;
    float sampleRate = state.metadata.sampleRate;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);

        const bool hasPreview = state.importPhase == 3 && !state.previewSamples.empty();
        const auto& sourceSamples = hasPreview ? state.previewSamples : state.samples;

        sampleCount = sourceSamples.size();
        hasData = !sourceSamples.empty();
        if (hasData) {
            if (!state.reconstructedAudio.empty() && sampleRate > 0.0f) {
                const uint32_t numChannels = state.metadata.channels > 0 ? state.metadata.channels : 1;
                const size_t totalFrames = state.reconstructedAudio.size() / numChannels;
                duration = static_cast<double>(totalFrames) / static_cast<double>(sampleRate);
            } else {
                duration = sourceSamples.back().timestamp;
            }
        }
    }

    const char* statusText = state.isRecording ? "RECORDING" : "IDLE";
    const int displayedFft = fftSize > 0 ? fftSize : state.fallbackFftSize;
    const int displayedHop = hopSize > 0 ? hopSize : state.fallbackHopSize;
    const float displayedRate = sampleRate > 0.0f ? sampleRate : state.fallbackSampleRate;

    char fftStr[32], hopStr[32], rateStr[32];
    if (displayedFft > 0) {
        snprintf(fftStr, sizeof(fftStr), "%d", displayedFft);
    } else {
        snprintf(fftStr, sizeof(fftStr), "--");
    }
    if (displayedHop > 0) {
        snprintf(hopStr, sizeof(hopStr), "%d", displayedHop);
    } else {
        snprintf(hopStr, sizeof(hopStr), "--");
    }
    if (displayedRate > 0.0f) {
        snprintf(rateStr, sizeof(rateStr), "%.0fHz", static_cast<double>(displayedRate));
    } else {
        snprintf(rateStr, sizeof(rateStr), "--");
    }

    char statusBar[512];
    snprintf(statusBar, sizeof(statusBar),
             "STATUS: %s  |  RGB: %.3f %.3f %.3f  |  FRAMES: %zu  |  DURATION: %.2fs  |  FFT: %s  |  HOP: %s  |  RATE: %s",
             statusText,
             static_cast<double>(currentR), static_cast<double>(currentG), static_cast<double>(currentB),
             sampleCount,
             duration,
             fftStr, hopStr, rateStr);

    const float statusBarWidth = ImGui::CalcTextSize(statusBar).x;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float rightAlignX = std::max(0.0f, availableWidth - statusBarWidth);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rightAlignX);
    ImGui::Text("%s", statusBar);
    ImGui::Spacing();

    const float availableHeight = ImGui::GetContentRegionAvail().y - CONTROL_HEIGHT;
    const ImVec2 gradientSize(ImGui::GetContentRegionAvail().x, availableHeight);

    std::vector<ReSyne::Timeline::TimelineSample> previewData;
    if (hasData) {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        previewData = ReSyne::UI::samplePreviewData(state, ReSyne::UI::MAX_PREVIEW_SAMPLES_FULL_WINDOW, lock);
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
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        const ImVec2 regionMax(cursorPos.x + gradientSize.x, cursorPos.y + gradientSize.y);
        state.timeline.gradientRegionMin = cursorPos;
        state.timeline.gradientRegionMax = regionMax;
        state.timeline.gradientRegionValid = gradientSize.x > 1.0f && gradientSize.y > 1.0f;

        const ImU32 liveCol = ImGui::ColorConvertFloat4ToU32(ImVec4(currentR, currentG, currentB, 1.0f));
        drawList->AddRectFilled(cursorPos, regionMax, liveCol);
        drawList->AddRect(cursorPos, regionMax, IM_COL32(60, 60, 60, 255), 0.0f, 0, 1.0f);
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

        const bool audioActive = audioInput.isStreamActive();
        if (!audioActive) {
            constexpr const char* loadMessage = "Load or drag a supported file into the timeline, or load an active input via the sidebar.";
            ImFont* font = ImGui::GetFont();
            const float smallFontSize = ImGui::GetFontSize() * 0.9f;
            const ImVec2 textSize = font->CalcTextSizeA(
                smallFontSize,
                std::numeric_limits<float>::max(),
                0.0f,
                loadMessage);
            const ImVec2 textPos(
                cursorPos.x + (gradientSize.x - textSize.x) * 0.5f,
                cursorPos.y + (gradientSize.y - textSize.y) * 0.5f);
            drawList->AddText(
                font,
                smallFontSize,
                textPos,
                IM_COL32(160, 160, 160, 255),
                loadMessage);
        }
    }

    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    Recorder::handleKeyboardShortcuts(state, windowFocused, hasData && !state.isRecording);

    constexpr float BUTTON_WIDTH = 80.0f;
    constexpr float BUTTON_HEIGHT = 26.0f;
    constexpr float BUTTON_SPACING = 8.0f;

    const int toolbarButtonCount = 3;
    const int transportButtonCount = 3;
    const float toolbarButtonSpacing = 4.0f;
    const float transportSpacing = BUTTON_SPACING;
    const float playbackWidth = transportButtonCount * BUTTON_HEIGHT +
                                std::max(0, transportButtonCount - 1) * transportSpacing;
    const float toolbarWidth = toolbarButtonCount * BUTTON_HEIGHT +
                               std::max(0, toolbarButtonCount - 1) * toolbarButtonSpacing;
    const float controlsSpacing = transportSpacing * 1.5f;
    const float lockToExportSpacing = 6.0f;
    const float lockAndExportWidth = BUTTON_HEIGHT + lockToExportSpacing + BUTTON_WIDTH;
    const float rightGroupWidth = toolbarWidth + controlsSpacing + lockAndExportWidth;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 0.0f));
    if (ImGui::BeginTable("RecorderFullControls",
                          3,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("Recorder", ImGuiTableColumnFlags_WidthFixed,
                                BUTTON_WIDTH * 3.0f + BUTTON_SPACING * 2.0f + 8.0f);
        ImGui::TableSetupColumn("Transport", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Utilities", ImGuiTableColumnFlags_WidthFixed, rightGroupWidth + 12.0f);

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginDisabled(state.isRecording);
        if (ImGui::Button("START", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT))) {
            startRecording(state, fftProcessor, 2048, 1024);
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, BUTTON_SPACING);

        ImGui::BeginDisabled(!state.isRecording);
        if (ImGui::Button("STOP##record", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT))) {
            stopRecording(state);
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, BUTTON_SPACING);

        ImGui::BeginDisabled(state.isRecording);
        if (ImGui::Button("LOAD", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT))) {
            state.shouldOpenLoadDialog = true;
        }
        ImGui::EndDisabled();

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
        if (ImGui::Button(ICON_FA_REPEAT, ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
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

        if (ImGui::Button(ICON_FA_BACKWARD_STEP, ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
            Recorder::seekPlayback(state, 0.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Jump to start");
        }

        ImGui::SameLine(0.0f, transportSpacing);

        if (ImGui::Button(playPauseIcon, ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
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
        toolbarContext.buttonHeight = BUTTON_HEIGHT;
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
        if (ImGui::Button(ICON_FA_CROSSHAIRS, ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
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

        if (ImGui::Button("Export", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT))) {
            state.showExportDialog = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Export recording to file");
        }

        ImGui::EndDisabled();

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 9.0f);

    ImGui::End();
    ImGui::PopStyleVar(2);

    drawExportDialog(state);
    drawLoadingDialog(state);
    drawExportingDialog(state);
}

}

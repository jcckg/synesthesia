#include "resyne/recorder/recorder.h"

#include <algorithm>
#include <array>
#include <vector>
#include <mutex>
#include <limits>
#include <string>

#include "audio/analysis/fft/fft_processor.h"
#include "audio/input/audio_input.h"
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
#include "renderer/presentation_resources.h"
#include "ui/styling/system_theme/system_theme_detector.h"

namespace ReSyne {

namespace {

void drawStatusBar(const char* statusText, const ImVec4& statusColour,
                   const std::array<std::string, 6>& segments) {
    constexpr const char* separator = " | ";
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float contentLeft = ImGui::GetCursorScreenPos().x;
    const float ellipsisWidth = ImGui::CalcTextSize("...").x;
    const float separatorWidth = ImGui::CalcTextSize(separator).x;
    float barWidth = ImGui::CalcTextSize("STATUS: ").x + ImGui::CalcTextSize(statusText).x;
    std::size_t visibleCount = 0;
    for (const auto& segment : segments) {
        const float extraWidth = separatorWidth + ImGui::CalcTextSize(segment.c_str()).x;
        const float overflowWidth = visibleCount + 1 < segments.size() ? separatorWidth + ellipsisWidth : 0.0f;
        if (barWidth + extraWidth + overflowWidth > availableWidth) break;
        barWidth += extraWidth;
        ++visibleCount;
    }
    if (visibleCount < segments.size()) barWidth += separatorWidth + ellipsisWidth;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, availableWidth - barWidth));
    ImGui::TextUnformatted("STATUS: ");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(statusColour, "%s", statusText);
    for (std::size_t i = 0; i < visibleCount; ++i) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(separator);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(segments[i].c_str());
    }
    if (visibleCount == segments.size()) {
        if (ImGui::BeginPopup("StatusDetails")) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return;
    }

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(separator);
    ImGui::SameLine(0.0f, 0.0f);
    const ImVec2 ellipsisPosition = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##StatusOverflow", ImVec2(ellipsisWidth, ImGui::GetTextLineHeight()))) {
        ImGui::OpenPopup("StatusDetails");
    }
    const ImVec2 anchorMin = ImGui::GetItemRectMin();
    const ImVec2 anchorMax = ImGui::GetItemRectMax();
    const bool anchorHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    const ImFontGlyph* dot = ImGui::GetFontBaked()->FindGlyph('.');
    const float ellipsisOffset = (ImGui::GetTextLineHeight() - dot->Y0 - dot->Y1) * 0.5f;
    ImGui::GetWindowDrawList()->AddText(ImVec2(ellipsisPosition.x, ellipsisPosition.y + ellipsisOffset),
        ImGui::GetColorU32(anchorHovered ? ImGuiCol_Text : ImGuiCol_TextDisabled), "...");

    float popupWidth = 0.0f;
    for (std::size_t i = visibleCount; i < segments.size(); ++i) {
        popupWidth = std::max(popupWidth, ImGui::CalcTextSize(segments[i].c_str()).x);
    }
    popupWidth = std::min(availableWidth, popupWidth + ImGui::GetStyle().WindowPadding.x * 2.0f +
        ImGui::GetStyle().WindowBorderSize * 2.0f);
    ImGui::SetNextWindowPos(ImVec2(std::clamp(anchorMin.x, contentLeft,
        contentLeft + availableWidth - popupWidth), anchorMax.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(popupWidth, 0.0f), ImVec2(popupWidth, FLT_MAX));
    if (ImGui::BeginPopup("StatusDetails", ImGuiWindowFlags_NoMove)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        for (std::size_t i = visibleCount; i < segments.size(); ++i) {
            ImGui::TextUnformatted(segments[i].c_str());
        }
        ImGui::PopTextWrapPos();
        if (!anchorHovered &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            !ImGui::IsWindowAppearing()) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}


}

void Recorder::drawFullWindow(RecorderState& state,
                              AudioInput& audioInput,
                              bool canStartRecording,
                              float windowX,
                              float windowY,
                              float windowWidth,
                              float windowHeight,
                              float currentR,
                              float currentG,
                              float currentB) {
    ImGui::SetNextWindowPos(ImVec2(windowX, windowY));
    ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
    constexpr float WINDOW_PADDING = ReSyne::UI::kToolbarHorizontalPadding;
    constexpr float HEADER_PADDING = 9.0f;
    constexpr float TOOLBAR_PADDING = ReSyne::UI::kToolbarVerticalPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(WINDOW_PADDING, TOOLBAR_PADDING));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGui::Begin("##ResyneFullWindow",
                 nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetCursorPosY(HEADER_PADDING);
    const bool exportRunning = state.exportRunning.load(std::memory_order_acquire);
    const bool controlsEnabled = state.importPhase == 0;

    if (state.focusRequested) {
        ImGui::SetWindowFocus();
        state.focusRequested = false;
    }

    const float toolbarWidth = windowWidth - WINDOW_PADDING * 2.0f;
    const float CONTROL_HEIGHT = UI::Utilities::recorderToolbarHeight(state, toolbarWidth);
    const float toolbarY = windowHeight - CONTROL_HEIGHT;
    size_t sampleCount = 0;
    double duration = 0.0;
    bool hasData = false;
    int fftSize = state.metadata.fftSize;
    int hopSize = state.metadata.hopSize;
    float sampleRate = state.metadata.sampleRate;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const auto presentation = recorderPresentation(state);
    if (presentation) {
        sampleCount = presentation->sourceFrames;
        hasData = presentation->colours->size() > 0;
        duration = presentation->duration;
    }

    const bool isPlaying = state.audioOutput && state.audioOutput->isPlaying();
    const char* statusText = state.importPhase != 0 ? "LOADING" : state.isRecording ? "RECORDING" : isPlaying ? "PLAYING" : "IDLE";
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

    char rgbStr[96], durationStr[64];
    snprintf(rgbStr, sizeof(rgbStr), "RGB: %.3f %.3f %.3f",
        static_cast<double>(currentR), static_cast<double>(currentG), static_cast<double>(currentB));
    snprintf(durationStr, sizeof(durationStr), "DURATION: %.2fs", duration);
    const std::array<std::string, 6> statusSegments{
        rgbStr, "FRAMES: " + std::to_string(sampleCount), durationStr,
        std::string("FFT: ") + fftStr, std::string("HOP: ") + hopStr, std::string("RATE: ") + rateStr
    };
    state.fileBrowser.playingColour = UI::activityStatusColour(state, audioInput, presentation.get());
    drawStatusBar(statusText, state.fileBrowser.playingColour, statusSegments);
    ImGui::Spacing();
    ImGui::BeginDisabled(!controlsEnabled);

    const float availableHeight = std::max(1.0f, toolbarY - ImGui::GetCursorPosY());
    const ImVec2 gradientSize(ImGui::GetContentRegionAvail().x, availableHeight);

    std::vector<ReSyne::Timeline::TimelineSample> previewData;

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
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        const ImVec2 regionMax(cursorPos.x + gradientSize.x, cursorPos.y + gradientSize.y);
        state.timeline.gradientRegionMin = cursorPos;
        state.timeline.gradientRegionMax = regionMax;
        state.timeline.gradientRegionValid = gradientSize.x > 1.0f && gradientSize.y > 1.0f;

        const bool isLightMode = SystemThemeDetector::isSystemInDarkMode() == false;
        bool usedHighPrecisionPreview = false;
        if (state.presentationResources != nullptr) {
            const ImTextureID textureId = state.presentationResources->updateActiveColourTexture(
                Renderer::PresentationResources::PreviewSurface::Recorder,
                isLightMode ? 0.9607843f : currentR,
                isLightMode ? 0.9607843f : currentG,
                isLightMode ? 0.9607843f : currentB);
            if (textureId != ImTextureID_Invalid) {
                drawList->AddImage(textureId, cursorPos, regionMax);
                usedHighPrecisionPreview = true;
            }
        }
        const ImU32 liveCol = isLightMode ? IM_COL32(245, 245, 245, 255) : ImGui::ColorConvertFloat4ToU32(ImVec4(currentR, currentG, currentB, 1.0f));
        const ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Separator);
        const float borderWidth = 1.0f / std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
        if (!usedHighPrecisionPreview) {
            drawList->AddRectFilled(cursorPos, regionMax, liveCol);
        }
        drawList->AddRect(cursorPos, regionMax, borderCol, 0.0f, borderWidth, ImDrawFlags_StrokeInside);
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
            bool preparing = state.importPhase != 0;
            {
                std::lock_guard lock(state.samplesMutex);
                preparing = preparing || !state.samples.empty() || state.metadata.presentationData != nullptr;
            }
            const char* loadMessage = preparing ? "Preparing track…" :
                "Load or drag a supported file into the timeline, or load an active input via the sidebar.";
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
            const ImU32 textCol = isLightMode ? IM_COL32(80, 80, 80, 255) : IM_COL32(160, 160, 160, 255);
            drawList->AddText(
                font,
                smallFontSize,
                textPos,
                textCol,
                loadMessage);
        }
    }

    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (controlsEnabled) ::UI::Input::handleTimelineShortcuts(state, windowFocused);
    if ((!canStartRecording || !controlsEnabled || exportRunning) && state.recordingCountdownActive) {
        RecorderUI::cancelRecordingCountdown(state);
    }
    if (RecorderUI::updateRecordingCountdown(state, ImGui::GetIO().DeltaTime)) {
        startRecording(state, audioInput.getAudioProcessor(), 0, 0);
    }

    ImGui::SetCursorPos(ImVec2(WINDOW_PADDING, toolbarY));
    UI::Utilities::RecorderToolbarContext toolbar;
    toolbar.enabled = controlsEnabled;
    toolbar.canStartRecording = canStartRecording;
    toolbar.hasData = hasData;
    toolbar.timelineHasBorder = true;
    toolbar.width = toolbarWidth;
    toolbar.outerLeftCorner = ::UI::FileBrowser::visibleWidth(state.fileBrowser) <= 0.0f || !state.fileBrowser.onLeft;
    toolbar.outerRightCorner = ::UI::FileBrowser::visibleWidth(state.fileBrowser) <= 0.0f || state.fileBrowser.onLeft;
    UI::Utilities::renderRecorderToolbar(state, toolbar);

    ImGui::EndDisabled();
    ImGui::End();
    ImGui::PopStyleVar(3);

    drawExportDialog(state);
    RecorderUI::drawClearTimelineDialog(state);
}

}

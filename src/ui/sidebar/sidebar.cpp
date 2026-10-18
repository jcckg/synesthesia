#include "sidebar.h"

#include "resyne/ui/timeline/timeline.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <imgui.h>

#include "controls.h"
#include "device_manager.h"
#include "renderer/presentation_resources.h"
#include "resyne/recorder/recorder.h"
#include "resyne/ui/recorder/recorder_constants.h"

namespace Sidebar {
namespace {

void renderHeader(const RenderArgs& args) {
    ImGui::SetCursorPosY(9.0f);

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    const float textWidth = ImGui::CalcTextSize("Synesthesia").x;
    ImGui::SetCursorPosX((args.layout.width - textWidth) * 0.5f);
    ImGui::Text("Synesthesia");
    ImGui::PopFont();
}

void renderFooter(const RenderArgs& args, bool showHideHint) {
    if (showHideHint) {
        ImGuiStyle& style = ImGui::GetStyle();
        const float textHeight = ImGui::GetTextLineHeightWithSpacing();
        const float reservedHeight = textHeight + args.layout.padding;
        const float availableHeight = ImGui::GetWindowHeight() - ImGui::GetCursorPosY() - style.WindowPadding.y;
        const float spaceToBottom = std::max(0.0f, availableHeight - reservedHeight);

        if (spaceToBottom > 0.0f) {
            ImGui::Dummy(ImVec2(0.0f, spaceToBottom));
        }

        ImGui::Separator();
        const float textWidth = ImGui::CalcTextSize("Press H to hide/show interface").x;
        ImGui::SetCursorPosX((args.layout.width - textWidth) * 0.5f);
        ImGui::TextDisabled("Press H to hide/show interface");
    }
}

void renderViewTabs(RenderArgs& args) {
    constexpr float TAB_HEIGHT = ReSyne::Timeline::RULER_HEIGHT;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float horizontalPadding = args.layout.padding;
    const float availableWidth = args.layout.width - horizontalPadding * 2.0f;
    const float buttonSpacing = style.ItemSpacing.x;
    const float buttonWidth = std::max(0.0f, (availableWidth - buttonSpacing) * 0.5f);

    constexpr float WINDOW_PADDING = 9.0f;
    const float timelineStartY = WINDOW_PADDING + ImGui::GetTextLineHeight() + 8.0f;
    ImGui::SetCursorPosY(timelineStartY);

    ImGui::SetCursorPosX(horizontalPadding);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(buttonSpacing, style.ItemSpacing.y));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 4.0f));

    struct TabDefinition {
        const char* label;
        UIState::View view;
    };

    const TabDefinition tabs[] = {
        {"ReSyne", UIState::View::ReSyne},
        {"Visualisation", UIState::View::Visualisation},
    };

    ImGui::PushID("SidebarViewTabs");
    for (int i = 0; i < 2; ++i) {
        const bool isActive = args.uiState.visualSettings.activeView == tabs[i].view;
        const bool detachedVisualisationOpen = args.recorderState.detachedVisualisation.isOpen;
        const bool tabEnabled =
            !(detachedVisualisationOpen && tabs[i].view == UIState::View::Visualisation);
        const ImVec4 baseColour = ImGui::GetStyleColorVec4(isActive ? ImGuiCol_ButtonActive : ImGuiCol_Button);
        const ImVec4 hoverColour = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        const ImVec4 activeColour = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        const ImVec4 textColour = ImGui::GetStyleColorVec4(ImGuiCol_Text);

        ImGui::PushStyleColor(ImGuiCol_Button, baseColour);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColour);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColour);
        ImGui::PushStyleColor(ImGuiCol_Text, textColour);

        ImGui::BeginDisabled(!tabEnabled);
        if (ImGui::Button(tabs[i].label, ImVec2(buttonWidth, TAB_HEIGHT))) {
            args.uiState.visualSettings.activeView = tabs[i].view;
        }
        ImGui::EndDisabled();
        if (!tabEnabled &&
            tabs[i].view == UIState::View::Visualisation &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", ReSyne::UI::kDetachedVisualisationAlreadyOpenTooltip);
        }

        ImGui::PopStyleColor(4);

        if (i + 1 < 2) {
            ImGui::SameLine();
        }
    }
    ImGui::PopID();

    ImGui::PopStyleVar(3);

    ImGui::Spacing();
    ImGui::Spacing();
}

void renderVisualisationSections(const RenderArgs& args,
                                 const bool liveInputActive,
                                 const bool showEQControls) {
    Controls::renderColourSmoothingPanel(
        args.uiState.visualSettings.colourSmoothingSpeed,
        args.uiState.visualSettings.manualSmoothing,
        args.uiState.visibility.showSpectrumAnalyser,
        args.layout.width,
        args.layout.padding,
        args.layout.buttonHeight,
        &args.uiState.resyneState.recorderState, args.uiState.presentationResponseSeconds, args.uiState.presentationResponseRange);

    if (showEQControls) {
        Controls::renderEQControlsPanel(
            args.uiState.audioSettings.lowGain,
            args.uiState.audioSettings.midGain,
            args.uiState.audioSettings.highGain,
            liveInputActive,
            args.layout.width,
            args.layout.padding,
            args.layout.labelWidth,
            args.layout.controlWidth,
            args.layout.buttonHeight,
            args.layout.contentWidth);
    }
}

void renderReSyneSections(const RenderArgs& args) {
    ImGui::SetCursorPosX(args.layout.padding);
    ImGui::PushID("ReSyneSections");

    const float contentWidth = args.layout.width - args.layout.padding * 2.0f;

    if (Controls::renderSectionHeader("Colour Preview")) {
        ImGui::Spacing();

        const float squareSize = contentWidth;

        ImGui::SetCursorPosX(args.layout.padding);
        const ImVec2 squarePos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("ReSyneColour", ImVec2(squareSize, squareSize));

        const float* colour = args.uiState.resyneState.displayColour;

        const ImVec4 colourVec(std::clamp(colour[0], 0.0f, 1.0f),
                               std::clamp(colour[1], 0.0f, 1.0f),
                               std::clamp(colour[2], 0.0f, 1.0f),
                               1.0f);

        const ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Border);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 squareMax(squarePos.x + squareSize, squarePos.y + squareSize);
        bool usedHighPrecisionPreview = false;
        if (args.uiState.presentationResources != nullptr) {
            const ImTextureID textureId = args.uiState.presentationResources->updateActiveColourTexture(
                Renderer::PresentationResources::PreviewSurface::Sidebar,
                colourVec.x,
                colourVec.y,
                colourVec.z);
            if (textureId != ImTextureID_Invalid) {
                drawList->AddImage(textureId, squarePos, squareMax);
                usedHighPrecisionPreview = true;
            }
        }
        if (!usedHighPrecisionPreview) {
            drawList->AddRectFilled(squarePos, squareMax, ImGui::ColorConvertFloat4ToU32(colourVec));
        }
        drawList->AddRect(squarePos, squareMax, borderCol, 0.0f, 0, 1.0f);
        ImGui::Spacing();
    }

    ImGui::PopID();
}

}

void render(RenderArgs& args) {
    float sidebarX = args.uiState.visibility.sidebarOnLeft ? 0.0f : (args.displaySize.x - args.layout.width);
    ImGui::SetNextWindowPos(ImVec2(sidebarX, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(args.layout.width, args.displaySize.y));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin(
        "Sidebar",
        nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    renderHeader(args);

    renderViewTabs(args);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(args.recorderState.exportRunning.load(std::memory_order_acquire));

    DeviceManager::renderDeviceSelection(
        args.uiState.deviceState,
        args.audioInput,
        args.devices,
        args.uiState.inputLevelMonitor,
        ReSyne::Recorder::hasLoadedAudio(args.recorderState) &&
            !args.recorderState.isRecording &&
            !args.recorderState.recordingCountdownActive);
    DeviceManager::renderOutputDeviceSelection(
        args.uiState.deviceState,
        args.outputDevices,
        args.recorderState.audioOutput != nullptr && args.recorderState.audioOutput->isPlaying(),
        args.recorderState.audioOutput != nullptr
            ? args.recorderState.audioOutput->getStereoLevels()
            : std::array<float, 2>{0.0f, 0.0f});

    const bool deviceSelected = args.uiState.deviceState.selectedDeviceIndex >= 0;
    const bool streamHealthy = !args.uiState.deviceState.streamError;
    const bool inputStreamActive = args.audioInput.isStreamActive();
    const bool hasLiveInput = (deviceSelected && streamHealthy && inputStreamActive) && !args.isPlaybackActive;
    const bool hasProcessedAudio = args.recorderState.isRecording || ReSyne::Recorder::hasLoadedAudio(args.recorderState);
    const bool showEQControls = !hasProcessedAudio;

    const bool showFrequencyInfo = (deviceSelected && streamHealthy && inputStreamActive) || args.isPlaybackActive;

    if (args.uiState.visualSettings.activeView == UIState::View::ReSyne) {
        if (showFrequencyInfo) {
            Controls::renderFrequencyInfoPanel(args.audioInput, args.recorderState);
        }
        renderReSyneSections(args);
        renderVisualisationSections(args, hasLiveInput, showEQControls);
    }

    if (hasLiveInput) {
        DeviceManager::renderChannelSelection(args.uiState.deviceState, args.audioInput, args.devices);
    }

    if (args.uiState.visualSettings.activeView == UIState::View::Visualisation && showFrequencyInfo) {
        Controls::renderFrequencyInfoPanel(args.audioInput, args.recorderState);
        renderVisualisationSections(args, hasLiveInput, showEQControls);
    }

    Controls::renderAdvancedSettingsPanel(args.uiState
#ifdef ENABLE_MIDI
                                         , args.midiInput
                                         , args.midiDevices
#endif
                                           );

    renderFooter(args, args.uiState.visualSettings.activeView != UIState::View::ReSyne);

    ImGui::EndDisabled();
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(2);

    const float scale = std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
    const float dividerWidth = 1.0f / scale;
    const ImVec2 position = ImGui::GetWindowPos();
    const float edge = args.uiState.visibility.sidebarOnLeft ? position.x + args.layout.width : position.x;
    const float dividerX = std::round(edge * scale) / scale -
        (args.uiState.visibility.sidebarOnLeft ? dividerWidth : 0.0f);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(dividerX, position.y),
        ImVec2(dividerX + dividerWidth, position.y + args.displaySize.y), ImGui::GetColorU32(ImGuiCol_Separator));

    ImGui::End();
    ImGui::PopStyleVar();
}

}

#include "sidebar.h"

#include "resyne/ui/timeline/timeline.h"

#include <algorithm>

#include <imgui.h>

#include "controls.h"
#include "controls.h"
#include "device_manager.h"
#include "ui/styling/system_theme/system_theme_detector.h"

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
    constexpr float TAB_HEIGHT = 23.0f;
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
        const bool isLightMode = SystemThemeDetector::isSystemInDarkMode() == false;

        ImVec4 baseColour;
        ImVec4 hoverColour;
        ImVec4 activeColour;
        ImVec4 textColour;

        if (isLightMode) {
             baseColour = isActive ? ImVec4(0.85f, 0.85f, 0.85f, 1.0f)
                                   : ImVec4(0.96f, 0.96f, 0.96f, 1.0f);
             hoverColour = isActive ? ImVec4(0.80f, 0.80f, 0.80f, 1.0f)
                                    : ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
             activeColour = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
             textColour = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        } else {
             baseColour = isActive ? ImVec4(0.25f, 0.25f, 0.25f, 1.0f)
                                   : ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
             hoverColour = isActive ? ImVec4(0.32f, 0.32f, 0.32f, 1.0f)
                                    : ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
             activeColour = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
             textColour = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, baseColour);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColour);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColour);
        ImGui::PushStyleColor(ImGuiCol_Text, textColour);

        if (ImGui::Button(tabs[i].label, ImVec2(buttonWidth, TAB_HEIGHT))) {
            args.uiState.visualSettings.activeView = tabs[i].view;
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

void renderVisualisationSections(const RenderArgs& args, bool showEQControls) {
    Controls::renderVisualiserSettingsPanel(
        args.colourSmoother,
        args.uiState.visualSettings.colourSmoothingSpeed,
        args.layout.width,
        args.layout.padding,
        args.layout.labelWidth,
        args.layout.controlWidth,
        args.layout.buttonHeight);

    if (showEQControls) {
        Controls::renderEQControlsPanel(
            args.uiState.audioSettings.lowGain,
            args.uiState.audioSettings.midGain,
            args.uiState.audioSettings.highGain,
            args.uiState.visibility.showSpectrumAnalyser,
            args.uiState.audioSettings.spectrumSmoothingFactor,
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

    if (ImGui::CollapsingHeader("Colour Preview")) {
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

        const bool isLightMode = SystemThemeDetector::isSystemInDarkMode() == false;
        const ImU32 borderCol = isLightMode ? IM_COL32(180, 180, 180, 255) : IM_COL32(90, 90, 90, 255);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 squareMax(squarePos.x + squareSize, squarePos.y + squareSize);
        drawList->AddRectFilled(squarePos, squareMax, ImGui::ColorConvertFloat4ToU32(colourVec));
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

    ImGui::Begin(
        "Sidebar",
        nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    renderHeader(args);

    renderViewTabs(args);

    DeviceManager::renderDeviceSelection(args.uiState.deviceState, args.audioInput, args.devices);
    DeviceManager::renderOutputDeviceSelection(args.uiState.deviceState, args.outputDevices);

    bool deviceSelected = args.uiState.deviceState.selectedDeviceIndex >= 0;
    bool streamHealthy = !args.uiState.deviceState.streamError;
    bool hasLiveInput = (deviceSelected && streamHealthy) && !args.isPlaybackActive;

    bool showFrequencyInfo = (deviceSelected && streamHealthy) || args.isPlaybackActive;

    if (args.uiState.visualSettings.activeView == UIState::View::ReSyne) {
        if (showFrequencyInfo) {
            Controls::renderFrequencyInfoPanel(args.audioInput, args.clearColour, args.uiState, args.recorderState);
        }
        renderReSyneSections(args);
    }

    if (hasLiveInput) {
        DeviceManager::renderChannelSelection(args.uiState.deviceState, args.audioInput, args.devices);
    }

    if (args.uiState.visualSettings.activeView == UIState::View::Visualisation && showFrequencyInfo) {
        Controls::renderFrequencyInfoPanel(args.audioInput, args.clearColour, args.uiState, args.recorderState);
        renderVisualisationSections(args, hasLiveInput);
    }

    Controls::renderAdvancedSettingsPanel(args.uiState, args.layout.contentWidth
#ifdef ENABLE_MIDI
                                           , args.midiInput
                                           , args.midiDevices
#endif
                                           );

    renderFooter(args, args.uiState.visualSettings.activeView != UIState::View::ReSyne);

    ImGui::End();
}

}

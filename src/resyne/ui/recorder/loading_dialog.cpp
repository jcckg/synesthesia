#include "resyne/recorder/recorder.h"

#include "imgui.h"
#include "ui/styling/system_theme/system_theme_detector.h"

namespace ReSyne {

void Recorder::drawLoadingDialog(RecorderState& state) {
    if (!state.showLoadingDialog) {
        return;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::OpenPopup("Loading...");

    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse;

    if (ImGui::BeginPopupModal("Loading...", nullptr, flags)) {
        ImGui::Text("Importing file:");
        ImGui::Spacing();

        const bool isLightMode = SystemThemeDetector::isSystemInDarkMode() == false;
        const ImVec4 filenameCol = isLightMode ? ImVec4(0.25f, 0.25f, 0.25f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, filenameCol);
        ImGui::TextWrapped("%s", state.loadingFilename.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        constexpr float PROGRESS_BAR_WIDTH = 400.0f;
        ImGui::ProgressBar(state.loadingProgress, ImVec2(PROGRESS_BAR_WIDTH, 0.0f));

        if (!state.loadingOperationStatus.empty()) {
            ImGui::Spacing();
            const ImVec4 statusCol = isLightMode ? ImVec4(0.4f, 0.4f, 0.4f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, statusCol);
            ImGui::TextWrapped("%s", state.loadingOperationStatus.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::EndPopup();
    }
}

}

#include "resyne/recorder/recorder.h"

#include "imgui.h"
#include "ui/styling/system_theme/system_theme_detector.h"

namespace ReSyne {

void Recorder::drawExportingDialog(RecorderState& state) {
    if (!state.showExportingDialog) {
        return;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::OpenPopup("Exporting...");

    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse;

    if (ImGui::BeginPopupModal("Exporting...", nullptr, flags)) {
        ImGui::Text("Exporting file:");
        ImGui::Spacing();

        const bool isLightMode = SystemThemeDetector::isSystemInDarkMode() == false;
        const ImVec4 filenameCol = isLightMode ? ImVec4(0.25f, 0.25f, 0.25f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, filenameCol);
        ImGui::TextWrapped("%s", state.exportingFilename.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float currentProgress = state.exportProgressAtomic.load(std::memory_order_acquire);

        constexpr float PROGRESS_BAR_WIDTH = 400.0f;
        ImGui::ProgressBar(currentProgress, ImVec2(PROGRESS_BAR_WIDTH, 0.0f));

        if (!state.exportOperationStatus.empty()) {
            ImGui::Spacing();
            const ImVec4 statusCol = isLightMode ? ImVec4(0.4f, 0.4f, 0.4f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, statusCol);
            ImGui::TextWrapped("%s", state.exportOperationStatus.c_str());
            ImGui::PopStyleColor();
        }

        if (state.exportComplete.load(std::memory_order_acquire)) {
            std::string errorMsg;
            {
                std::lock_guard<std::mutex> lock(state.samplesMutex);
                errorMsg = state.exportErrorMessage;
            }

            if (!errorMsg.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::TextWrapped("Export failed: %s", errorMsg.c_str());
                ImGui::PopStyleColor();
                ImGui::Spacing();

                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    state.showExportingDialog = false;
                    ImGui::CloseCurrentPopup();
                }
            } else {
                state.showExportingDialog = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

}

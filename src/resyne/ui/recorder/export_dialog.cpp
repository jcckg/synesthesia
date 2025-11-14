#include "resyne/recorder/recorder.h"

#include "imgui.h"
#include "utilities/video/ffmpeg_locator.h"

namespace ReSyne {

void Recorder::drawExportDialog(RecorderState& state) {
    if (!state.showExportDialog) {
        return;
    }

    ImGui::OpenPopup("Export as...");

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Export as...", &state.showExportDialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Select export format:");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        constexpr float BUTTON_WIDTH = 120.0f;
        constexpr float BUTTON_HEIGHT = 30.0f;

        if (ImGui::Button("WAV", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT))) {
            state.exportFormat = RecorderExportFormat::WAV;
            state.shouldOpenSaveDialog = true;
            state.showExportDialog = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Export as audio file");
        }

        ImGui::SameLine();

        if (ImGui::Button("TIFF", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT))) {
            state.exportFormat = RecorderExportFormat::TIFF;
            state.shouldOpenSaveDialog = true;
            state.showExportDialog = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Export as TIFF");
        }

        ImGui::SameLine();

        if (ImGui::Button(".resyne", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT))) {
            state.exportFormat = RecorderExportFormat::RESYNE;
            state.shouldOpenSaveDialog = true;
            state.showExportDialog = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Export as native format");
        }

        auto& ffmpegLocator = Utilities::Video::FFmpegLocator::instance();
        const bool ffmpegAvailable = ffmpegLocator.isAvailable();

        if (ffmpegAvailable) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Video:");
            ImGui::Spacing();

            if (ImGui::Button("MP4", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT))) {
                state.exportFormat = RecorderExportFormat::MP4;
                state.shouldOpenSaveDialog = true;
                state.showExportDialog = false;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Export as video");
            }

            ImGui::SameLine();

            ImGui::Checkbox("Export gradient", &state.exportGradient);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Also export a separate gradient video\nshowing colour history progression");
            }
        }

        ImGui::EndPopup();
    }

    if (!ImGui::IsPopupOpen("Export as...")) {
        state.showExportDialog = false;
    }
}

}

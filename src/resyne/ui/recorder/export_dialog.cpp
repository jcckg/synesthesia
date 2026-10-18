#include "resyne/recorder/recorder.h"

#include <algorithm>
#include <array>
#include <cfloat>

#include "imgui.h"
#include "ui/styling/styling.h"
#include "utilities/video/ffmpeg_locator.h"

namespace ReSyne {

namespace {

struct ExportOption {
    RecorderExportFormat format;
    const char* label;
    const char* description;
};

constexpr std::array<ExportOption, 4> kExportOptions{{
    {RecorderExportFormat::WAV, "WAV - Audio", "Audio reconstructed from this track."},
    {RecorderExportFormat::TIFF, "TIFF - Colour timeline", "Colour timeline as a lossless image."},
    {RecorderExportFormat::RSYN, "RSYN - ReSyne track", "Track and colour analysis for ReSyne."},
    {RecorderExportFormat::MP4, "MP4 - Video", "Colour visualisation with audio."}
}};

}

void Recorder::drawExportDialog(RecorderState& state) {
    if (!state.showExportDialog) return;

    ImGui::OpenPopup("Export");
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const float width = std::min(360.0f, viewport->WorkSize.x - 24.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    if (ImGui::BeginPopupModal("Export", &state.showExportDialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool videoAvailable = Utilities::Video::FFmpegLocator::instance().isAvailable();
        const int optionCount = videoAvailable ? static_cast<int>(kExportOptions.size()) : 3;
        int selected = 0;
        for (int index = 0; index < optionCount; ++index) {
            if (kExportOptions[static_cast<std::size_t>(index)].format == state.exportFormat) selected = index;
        }

        ImGui::TextDisabled("FORMAT");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (UIStyler::beginCombo("##ExportFormat", kExportOptions[static_cast<std::size_t>(selected)].label)) {
            for (int index = 0; index < optionCount; ++index) {
                if (ImGui::Selectable(kExportOptions[static_cast<std::size_t>(index)].label, selected == index)) selected = index;
                if (selected == index) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        const auto& option = kExportOptions[static_cast<std::size_t>(selected)];
        state.exportFormat = option.format;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", option.description);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();

        if (option.format == RecorderExportFormat::MP4) {
            ImGui::Spacing();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 1.5f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
            ImGui::Checkbox("Include gradient video", &state.exportGradient);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Also save a separate video of the colour timeline.");
            }
        } else if (!videoAvailable) {
            ImGui::TextDisabled("MP4 requires FFmpeg.");
        }

        ImGui::Spacing();
        constexpr ImVec2 buttonSize(80.0f, 26.0f);
        const float buttonsWidth = buttonSize.x * 2.0f + 8.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - buttonsWidth));
        if (ImGui::Button("Cancel", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            state.showExportDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button("Export", buttonSize)) {
            state.shouldOpenSaveDialog = true;
            state.showExportDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);

    if (!ImGui::IsPopupOpen("Export")) state.showExportDialog = false;
}

}

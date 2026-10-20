#include "resyne/recorder/recorder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"

namespace ReSyne {

namespace {

void drawIndicator(const ImVec2& min, const ImVec2& max, float fraction, const std::string& filename,
                   const std::string& operationStatus, bool alignLeft) {
    if (max.x - min.x < 32.0f || max.y - min.y < 24.0f) return;
    const float progress = std::isfinite(fraction) ? std::clamp(fraction, 0.0f, 1.0f) : 0.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool lightMode = ImGui::GetStyleColorVec4(ImGuiCol_Text).x < 0.5f;
    const ImU32 backgroundColour = lightMode ? ImGui::GetColorU32(ImGuiCol_Button) : IM_COL32(0, 0, 0, 96);
    const ImU32 progressColour = lightMode ? ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.4f, 1.0f)) : IM_COL32_WHITE;
    drawList->AddRectFilled(ImVec2(min.x, max.y - 3.0f), max,
        backgroundColour);
    drawList->AddRectFilled(ImVec2(min.x, max.y - 3.0f),
        ImVec2(min.x + (max.x - min.x) * progress, max.y), progressColour);

    char percentage[16];
    std::snprintf(percentage, sizeof(percentage), "%.0f%%", static_cast<double>(progress * 100.0f));
    const std::string label = std::string(percentage) + "  " + filename;
    const float width = std::min(ImGui::CalcTextSize(label.c_str()).x + 16.0f, max.x - min.x - 16.0f);
    const ImVec2 badgeMin(alignLeft ? min.x + 8.0f : max.x - width - 8.0f, min.y + 8.0f);
    const ImVec2 badgeMax(badgeMin.x + width, badgeMin.y + ImGui::GetFontSize() + 12.0f);
    drawList->AddRectFilled(badgeMin, badgeMax, ImGui::GetColorU32(ImGuiCol_WindowBg));
    drawList->PushClipRect(badgeMin, badgeMax, true);
    drawList->AddText(ImVec2(badgeMin.x + 8.0f, badgeMin.y + 6.0f),
        ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
    drawList->PopClipRect();
    if (ImGui::IsMouseHoveringRect(badgeMin, badgeMax)) {
        ImGui::SetTooltip("%s\n%s", filename.c_str(), operationStatus.c_str());
    }
}

void drawExportError(RecorderState& state, const std::string& error) {
    ImGui::OpenPopup("Export failed");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Export failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0f);
        ImGui::TextWrapped("%s", state.exportingFilename.c_str());
        ImGui::TextWrapped("%s", error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            state.showExportingDialog = false;
            setExportOperationStatus(state, {});
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}

void Recorder::drawProgressIndicator(RecorderState& state, const ImVec2& regionMin, const ImVec2& regionMax) {
    std::string error;
    if (state.showExportingDialog && state.exportComplete.load(std::memory_order_acquire)) {
        {
            std::lock_guard lock(state.samplesMutex);
            error = state.exportErrorMessage;
        }
        if (error.empty()) {
            state.showExportingDialog = false;
            setExportOperationStatus(state, {});
        }
    }

    if (!state.showExportingDialog && !state.showLoadingDialog) return;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##OperationProgress", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing);
    if (!error.empty()) {
        drawExportError(state, error);
    } else if (state.showExportingDialog) {
        drawIndicator(regionMin, regionMax, state.exportProgressAtomic.load(std::memory_order_acquire),
            state.exportingFilename, getExportOperationStatus(state), state.progressIndicatorOnLeft);
    } else if (state.showLoadingDialog) {
        drawIndicator(regionMin, regionMax, state.loadingProgress, state.loadingFilename, getLoadingOperationStatus(state), state.progressIndicatorOnLeft);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

}

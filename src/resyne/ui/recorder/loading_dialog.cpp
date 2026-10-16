#include "resyne/recorder/recorder.h"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

namespace ReSyne {

void Recorder::drawLoadingDialog(RecorderState& state) {
    if (!state.showLoadingDialog || !state.timeline.gradientRegionValid) return;

    const ImVec2 min = state.timeline.gradientRegionMin;
    const ImVec2 max = state.timeline.gradientRegionMax;
    if (max.x - min.x < 32.0f || max.y - min.y < 24.0f) return;
    const float progress = std::clamp(state.loadingProgress, 0.0f, 1.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(min.x, max.y - 3.0f), max,
        ImGui::GetColorU32(ImGuiCol_FrameBg));
    drawList->AddRectFilled(ImVec2(min.x, max.y - 3.0f),
        ImVec2(min.x + (max.x - min.x) * progress, max.y), ImGui::GetColorU32(ImGuiCol_Text));

    char percentage[16];
    std::snprintf(percentage, sizeof(percentage), "%.0f%%", static_cast<double>(progress * 100.0f));
    const std::string label = std::string(percentage) + "  " + state.loadingFilename;
    const float width = std::min(ImGui::CalcTextSize(label.c_str()).x + 16.0f, max.x - min.x - 16.0f);
    const ImVec2 badgeMin(max.x - width - 8.0f, min.y + 8.0f);
    const ImVec2 badgeMax(max.x - 8.0f, badgeMin.y + ImGui::GetFontSize() + 12.0f);
    drawList->AddRectFilled(badgeMin, badgeMax, ImGui::GetColorU32(ImGuiCol_WindowBg), 3.0f);
    drawList->PushClipRect(badgeMin, badgeMax, true);
    drawList->AddText(ImVec2(badgeMin.x + 8.0f, badgeMin.y + 6.0f),
        ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
    drawList->PopClipRect();
    if (ImGui::IsMouseHoveringRect(badgeMin, badgeMax)) {
        const auto status = getLoadingOperationStatus(state);
        ImGui::SetTooltip("%s\n%s", state.loadingFilename.c_str(), status.c_str());
    }
}

}

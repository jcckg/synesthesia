#include "ui/notifications/status_prompt.h"

#include <algorithm>
#include <utility>

namespace UI::Notifications {

void showStatusPrompt(StatusPrompt& prompt, std::string message) {
    prompt.message = std::move(message);
    prompt.remainingSeconds = 2.5f;
}

void updateStatusPrompt(StatusPrompt& prompt, float deltaSeconds) {
    prompt.remainingSeconds = std::max(0.0f, prompt.remainingSeconds - std::max(0.0f, deltaSeconds));
    if (prompt.remainingSeconds == 0.0f) prompt.message.clear();
}

void drawStatusPrompt(const StatusPrompt& prompt, const ImVec2& regionMin, const ImVec2& regionMax,
                      bool alignLeft, float topOffset) {
    if (prompt.message.empty() || prompt.remainingSeconds <= 0.0f) return;

    const float height = ImGui::GetFontSize() + 12.0f;
    const float availableWidth = regionMax.x - regionMin.x - 16.0f;
    if (availableWidth <= 16.0f || regionMax.y - regionMin.y < height + topOffset + 16.0f) return;

    const float width = std::min(ImGui::CalcTextSize(prompt.message.c_str()).x + 16.0f, availableWidth);
    const ImVec2 position(alignLeft ? regionMin.x + 8.0f : regionMax.x - width - 8.0f,
                          regionMin.y + 8.0f + topOffset);
    const float alpha = std::min(1.0f, prompt.remainingSeconds / 0.5f);
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
    ImGui::Begin("##StatusPrompt", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    auto* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(ImVec2(position.x + 8.0f, position.y),
                           ImVec2(position.x + width - 8.0f, position.y + height), true);
    drawList->AddText(ImVec2(position.x + 8.0f, position.y + 6.0f),
                       ImGui::GetColorU32(ImGuiCol_Text), prompt.message.c_str());
    drawList->PopClipRect();
    ImGui::End();
    ImGui::PopStyleVar(5);
}

}

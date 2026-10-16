#include "resyne/ui/recorder/shared_components.h"

#include "imgui.h"

namespace ReSyne::UI {

void renderStatusMessage(RecorderState& state) {
    if (state.statusMessageTimer > 0.0f && !state.statusMessage.empty()) {
        ImGui::Spacing();

        constexpr float FADE_DURATION = 1.0f;
        float alpha = 1.0f;
        if (state.statusMessageTimer < FADE_DURATION) {
            alpha = state.statusMessageTimer / FADE_DURATION;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, alpha * 0.9f));
        ImGui::TextWrapped("%s", state.statusMessage.c_str());
        ImGui::PopStyleColor();
    }
}

}

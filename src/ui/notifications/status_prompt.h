#pragma once

#include <string>

#include "imgui.h"

namespace UI::Notifications {

struct StatusPrompt {
    std::string message;
    float remainingSeconds = 0.0f;
};

void showStatusPrompt(StatusPrompt& prompt, std::string message);
void updateStatusPrompt(StatusPrompt& prompt, float deltaSeconds);
void drawStatusPrompt(const StatusPrompt& prompt, const ImVec2& regionMin, const ImVec2& regionMax,
                      bool alignLeft, float topOffset = 0.0f);

}

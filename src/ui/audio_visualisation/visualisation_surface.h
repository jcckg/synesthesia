#pragma once

#include <array>

#include "imgui.h"

class AudioInput;
struct UIState;

namespace UI::AudioVisualisation {

struct SurfaceLayout {
    ImVec2 displaySize = ImVec2(0.0f, 0.0f);
    float sidebarWidth = 0.0f;
    bool sidebarOnLeft = false;
    float bottomPanelHeight = 0.0f;
};

std::array<float, 4> currentVisualisationClearColour(const UIState& state);

void renderSpectrumOverlay(UIState& state,
                           const AudioInput& audioInput,
                           const SurfaceLayout& layout,
                           bool hasPlaybackSession);

} // namespace UI::AudioVisualisation

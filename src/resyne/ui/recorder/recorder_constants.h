#pragma once

#include <imgui.h>

namespace ReSyne::UI {

inline float toolbarButtonHeight() { return ImGui::GetFrameHeight(); }
inline constexpr float kToolbarEdgePadding = 6.0f;
inline constexpr float kToolbarVerticalPadding = kToolbarEdgePadding;
inline constexpr float kToolbarHorizontalPadding = kToolbarEdgePadding;

inline constexpr const char* kDetachedVisualisationTooltip =
    "Open detached visualiser";
inline constexpr const char* kCloseDetachedVisualisationTooltip =
    "Close detached visualiser";
inline constexpr const char* kDetachedVisualisationAlreadyOpenTooltip =
    "Visualisation Mode is already open in another window";
inline constexpr const char* kDetachedVisualisationUnsupportedTooltip =
    "Detached visualisation is not supported by the current renderer";

}

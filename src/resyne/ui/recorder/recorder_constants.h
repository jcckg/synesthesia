#pragma once

#include <cstddef>

namespace ReSyne::UI {

constexpr size_t MAX_PREVIEW_SAMPLES_FULL_WINDOW = 1200;
constexpr size_t MAX_PREVIEW_SAMPLES_BOTTOM_PANEL = 900;
inline constexpr const char* kDetachedVisualisationTooltip =
    "Open Visualisation Mode in another window";
inline constexpr const char* kDetachedVisualisationAlreadyOpenTooltip =
    "Visualisation Mode is already open in another window";
inline constexpr const char* kDetachedVisualisationUnsupportedTooltip =
    "Detached visualisation is not supported by the current renderer";

}

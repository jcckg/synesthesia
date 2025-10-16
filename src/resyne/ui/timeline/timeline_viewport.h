#pragma once

#include "timeline.h"

namespace ReSyne::Timeline::Viewport {

constexpr float MIN_ZOOM_FACTOR = 1.0f;
constexpr float MAX_ZOOM_FACTOR = 8.0f;
constexpr float ZOOM_SENSITIVITY = 0.0125f;
constexpr float TRACKPAD_PINCH_SENSITIVITY = 1.35f;
constexpr float TRACKPAD_PAN_SENSITIVITY = 0.0035f;

float clampNormalised(float value);
void constrainView(float& start, float& end);
void computeViewWindow(TimelineState& state,
                       float visibleFraction,
                       float& start,
                       float& end,
                       float& span);

float computeVisibleFraction(float zoomFactor);

}

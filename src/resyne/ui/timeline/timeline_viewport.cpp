#include "timeline_viewport.h"

#include <algorithm>
#include <cmath>

namespace ReSyne::Timeline::Viewport {

float clampNormalised(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

void constrainView(float& start, float& end) {
    const float span = end - start;
    if (span <= 0.0f) {
        start = 0.0f;
        end = 1.0f;
        return;
    }

    if (start < 0.0f) {
        end -= start;
        start = 0.0f;
    }
    if (end > 1.0f) {
        start -= (end - 1.0f);
        end = 1.0f;
    }

    start = std::clamp(start, 0.0f, 1.0f);
    end = std::clamp(end, 0.0f, 1.0f);
    if (end <= start) {
        end = std::min(1.0f, start + 1e-6f);
        if (end <= start) {
            start = std::max(0.0f, end - 1e-6f);
        }
    }
}

void computeViewWindow(TimelineState& state,
                       float visibleFraction,
                       float& start,
                       float& end,
                       float& span) {
    if (visibleFraction >= 0.999f) {
        start = 0.0f;
        end = 1.0f;
        span = 1.0f;
        state.viewCentreNormalised = 0.5f;
        return;
    }

    if (state.trackScrubber) {
        state.viewCentreNormalised = state.scrubberNormalisedPosition;
    }

    const float halfSpan = visibleFraction * 0.5f;
    const float minCenter = halfSpan;
    const float maxCenter = 1.0f - halfSpan;

    if (minCenter >= maxCenter) {
        start = 0.0f;
        end = 1.0f;
        span = 1.0f;
        state.viewCentreNormalised = 0.5f;
        return;
    }

    const float centre = std::clamp(state.viewCentreNormalised, minCenter, maxCenter);
    span = visibleFraction;
    start = std::clamp(centre - halfSpan, 0.0f, 1.0f - span);
    end = start + span;
    state.viewCentreNormalised = centre;
}

float computeVisibleFraction(float zoomFactor) {
    if (zoomFactor <= MIN_ZOOM_FACTOR + 1e-4f) {
        return 1.0f;
    }
    return std::clamp(1.0f / zoomFactor, 0.02f, 1.0f);
}

}

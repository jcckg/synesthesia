#pragma once

#include <algorithm>
#include <cmath>

namespace UI::FileBrowser {

struct Resizing {
    static constexpr float defaultWidth = 280.0f;
    static constexpr float minimumWidth = 180.0f;
    static constexpr float maximumWidth = 480.0f;
    static constexpr float minimumContentWidth = 620.0f;
    static constexpr float collapseWidth = 96.0f;
    static constexpr float reopenWidth = 112.0f;
    static constexpr float snapDistance = 6.0f;
    static constexpr float snapReleaseDistance = 12.0f;

    float width = defaultWidth;
    float startWidth = defaultWidth;
    float startPointer = 0.0f;
    float previousWidth = defaultWidth;
    bool active = false;
    bool onLeft = false;
    bool collapsed = false;
    bool snapped = false;
    bool snapArmed = false;

    void begin(float pointer, bool left) {
        active = true;
        onLeft = left;
        startWidth = previousWidth = width;
        startPointer = pointer;
        collapsed = snapped = false;
        snapArmed = std::abs(width - defaultWidth) > snapReleaseDistance;
    }

    bool drag(float pointer, float availableWidth) {
        const float upper = std::clamp(availableWidth, minimumWidth, maximumWidth);
        const float requested = startWidth + (pointer - startPointer) * (onLeft ? 1.0f : -1.0f);
        const float distance = requested - defaultWidth;
        if (std::abs(distance) > snapReleaseDistance) snapArmed = true;
        const bool crossed = (previousWidth - defaultWidth) * distance <= 0.0f;
        previousWidth = requested;
        if (requested < (collapsed ? reopenWidth : collapseWidth)) {
            collapsed = true;
            snapped = false;
            return false;
        }
        collapsed = false;
        if (snapped && (std::abs(distance) > snapReleaseDistance || upper < defaultWidth)) snapped = false;
        bool feedback = false;
        if (!snapped && snapArmed && upper >= defaultWidth &&
            (std::abs(distance) <= snapDistance || (crossed && std::abs(distance) <= snapReleaseDistance))) {
            snapped = true;
            feedback = true;
        }
        width = snapped ? defaultWidth : std::clamp(requested, minimumWidth, upper);
        return feedback;
    }

    void finish() {
        active = snapped = false;
    }

    void cancel() {
        width = startWidth;
        active = collapsed = snapped = false;
    }
};

}

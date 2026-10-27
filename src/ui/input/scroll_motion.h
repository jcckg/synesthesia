#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace UI::Input {

struct ScrollMotion {
    static constexpr float rowPadding = 6.0f;
    float position = 0.0f;
    float target = 0.0f;
    float idleSeconds = 1.0f;
    float opacity = 0.0f;
    bool dragging = false;
    bool thumbHovered = false;
    float dragOffset = 0.0f;
    uint64_t resetGeneration = 0;

    void reset(float value = 0.0f) {
        ++resetGeneration;
        position = target = value;
        idleSeconds = 1.0f;
        opacity = 0.0f;
        dragging = thumbHovered = false;
    }

    void advance(float pixels, float steps, float rowHeight, float maximum, float delta) {
        maximum = std::max(0.0f, maximum);
        if (pixels != 0.0f) target = position = std::clamp(position - pixels, 0.0f, maximum);
        target = std::clamp(target - steps * rowHeight, 0.0f, maximum);
        const float previous = position;
        position += (target - position) * (1.0f - std::exp(-delta / 0.055f));
        if (std::abs(target - position) < 0.1f) position = target;
        position = std::clamp(position, 0.0f, maximum);
        const bool active = pixels != 0.0f || steps != 0.0f || dragging || std::abs(previous - position) > 0.1f;
        idleSeconds = active ? 0.0f : idleSeconds + delta;
        const float desired = idleSeconds < 0.65f && maximum > 0.0f ? 1.0f : 0.0f;
        opacity += (desired - opacity) * (1.0f - std::exp(-delta / (desired > opacity ? 0.04f : 0.16f)));
    }
};

}

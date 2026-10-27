#pragma once

#include <algorithm>
#include <cmath>

namespace UI::FileBrowser {

struct PanelAnimation {
    float value = 0.0f;
    float start = 0.0f;
    float target = 0.0f;
    float elapsed = 0.24f;
    int frame = -1;

    void reset(float nextValue) {
        value = start = target = nextValue;
        elapsed = 0.24f;
        frame = -1;
    }

    void advance(bool open, float delta, int nextFrame) {
        if (frame == nextFrame) return;
        frame = nextFrame;
        const float nextTarget = open ? 1.0f : 0.0f;
        if (target != nextTarget) {
            start = value;
            target = nextTarget;
            elapsed = 0.0f;
        }
        elapsed = std::min(0.24f, elapsed + std::clamp(delta, 0.0f, 0.1f));
        const float progress = elapsed / 0.24f;
        const float ease = progress * progress * (3.0f - 2.0f * progress);
        value = start + (target - start) * ease;
    }
};

inline float wrappingTextOffset(float elapsed, float repeatWidth, bool towardsRight) {
    if (repeatWidth <= 0.0f) return 0.0f;
    const float phase = std::fmod(std::max(0.0f, elapsed) * 34.0f, repeatWidth);
    return towardsRight ? phase - repeatWidth : -phase;
}

}

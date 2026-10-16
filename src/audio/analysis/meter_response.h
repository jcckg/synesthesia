#pragma once

#include <algorithm>
#include <cmath>

namespace AudioMeter {

// https://www.dsprelated.com/freebooks/filters/Time_Constant_One_Pole.html
inline float peakResponse(float current, float target, double elapsed) {
    current = std::isfinite(current) ? std::clamp(current, 0.0f, 1.0f) : 0.0f;
    target = std::isfinite(target) ? std::clamp(target, 0.0f, 1.0f) : 0.0f;
    elapsed = std::isfinite(elapsed) ? std::max(0.0, elapsed) : 0.0;
    return std::max(target, current * static_cast<float>(std::exp(std::log(.05) * elapsed / .35)));
}

}

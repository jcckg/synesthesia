#include "smoothing.h"
#include <algorithm>
#include <cmath>

float manualResponseTime(float amount) {
    amount = std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.6f;
    return 0.04f * std::pow(1.5f / 0.04f, amount);
}

float manualSmoothingAmount(float seconds) {
    seconds = std::isfinite(seconds) ? std::clamp(seconds, 0.04f, 1.5f) : manualResponseTime(.6f);
    return std::log(seconds / 0.04f) / std::log(1.5f / 0.04f);
}

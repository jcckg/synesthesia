#include "timeline_labels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace ReSyne::Timeline::Labels {

namespace {

constexpr std::array<double, 21> STEPS = {
    0.01, 0.02, 0.05,
    0.1, 0.2, 0.5,
    1.0, 2.0, 5.0,
    10.0, 15.0, 30.0,
    60.0, 120.0, 300.0,
    600.0, 900.0, 1800.0,
    3600.0, 5400.0, 7200.0
};

}

double chooseMajorTickStep(double durationSeconds, double maximumTicks) {
    if (durationSeconds <= 0.0) {
        return 1.0;
    }

    for (double step : STEPS) {
        double count = durationSeconds / step;
        if (count <= std::max(maximumTicks, 1.0)) {
            return step;
        }
    }

    return STEPS.back();
}

int chooseMinorTickDivisions(double majorStep, double majorSpacing) {
    if (!std::isfinite(majorStep) || !std::isfinite(majorSpacing) || majorStep <= 0.0 || majorSpacing < 16.0)
        return 1;
    for (double step : STEPS) {
        if (step >= majorStep) break;
        const double divisions = majorStep / step;
        if (divisions < 2.0 || divisions > 20.0 || majorSpacing / divisions < 8.0) continue;
        if (std::abs(divisions - std::round(divisions)) < 1e-6)
            return static_cast<int>(std::round(divisions));
    }
    return 1;
}

std::string formatTickLabel(double seconds, double majorStep, bool showHours) {
    seconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
    int decimals = 0;
    std::int64_t scale = 1;
    while (std::isfinite(majorStep) && majorStep > 0.0 && decimals < 3 &&
        std::abs(majorStep * static_cast<double>(scale) - std::round(majorStep * static_cast<double>(scale))) > 1e-6) {
        ++decimals;
        scale *= 10;
    }
    const auto units = static_cast<std::int64_t>(std::llround(seconds * static_cast<double>(scale)));
    const auto wholeSeconds = units / scale;
    std::ostringstream oss;
    oss << std::setfill('0');
    if (showHours || wholeSeconds >= 3600) {
        oss << std::setw(2) << wholeSeconds / 3600 << ':';
    }
    oss << std::setw(2) << (wholeSeconds / 60) % 60 << ':' << std::setw(2) << wholeSeconds % 60;
    if (decimals > 0) oss << '.' << std::setw(decimals) << units % scale;
    return oss.str();
}

}

#include "timeline_labels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ReSyne::Timeline::Labels {

double chooseMajorTickStep(double durationSeconds) {
    if (durationSeconds <= 0.0) {
        return 1.0;
    }

    constexpr std::array<double, 18> STEPS = {
        0.1, 0.2, 0.5,
        1.0, 2.0, 5.0,
        10.0, 15.0, 30.0,
        60.0, 120.0, 300.0,
        600.0, 900.0, 1800.0,
        3600.0, 5400.0, 7200.0
    };

    for (double step : STEPS) {
        double count = durationSeconds / step;
        if (count <= 8.0) {
            return step;
        }
    }

    return STEPS.back();
}

std::string formatTickLabel(double seconds, double majorStep) {
    seconds = std::max(0.0, seconds);

    if (seconds >= 3600.0) {
        int hours = static_cast<int>(seconds / 3600.0);
        int minutes = static_cast<int>(std::fmod(seconds, 3600.0) / 60.0);
        int secs = static_cast<int>(std::round(std::fmod(seconds, 60.0)));
        if (secs == 60) {
            secs = 0;
            ++minutes;
        }
        if (minutes >= 60) {
            minutes -= 60;
            ++hours;
        }
        std::ostringstream oss;
        oss << hours << ':' << std::setfill('0') << std::setw(2) << minutes << ':' << std::setw(2) << secs;
        return oss.str();
    }

    if (seconds >= 60.0) {
        int minutes = static_cast<int>(seconds / 60.0);
        int secs = static_cast<int>(std::round(std::fmod(seconds, 60.0)));
        if (secs == 60) {
            secs = 0;
            ++minutes;
        }
        std::ostringstream oss;
        oss << minutes << ':' << std::setfill('0') << std::setw(2) << secs;
        return oss.str();
    }

    int decimals = 0;
    if (majorStep < 0.25) {
        decimals = 2;
    } else if (majorStep < 1.0) {
        decimals = 1;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimals) << seconds << 's';
    return oss.str();
}

}

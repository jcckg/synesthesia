#pragma once

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

#include "colour/spectral_contributions.h"

namespace UI::Spectrum {

// https://www.itl.nist.gov/div898/handbook/eda/section3/eda33e.htm
inline std::vector<double> contributionDensity(std::span<const float> contributions) {
    std::vector<double> density(contributions.size());
    if (contributions.size() < 2) return density;
    const double minimum = ColourPresentation::contributionMinimumFrequency;
    const double maximum = ColourPresentation::contributionMaximumFrequency;
    const double octaveWidth = std::log2(maximum / minimum) / static_cast<double>(contributions.size()-1);
    double total = 0.0;
    for (const float contribution : contributions)
        if (std::isfinite(contribution) && contribution > 0.0f) total += contribution;
    // Show relative colour presence; the soft floor lets quiet contributions fade instead of normalising silence upwards.
    constexpr double silenceReference = .01;
    const double normalising = total + silenceReference;
    for (std::size_t i = 0; i < contributions.size(); ++i) {
        const double value = std::isfinite(contributions[i]) ? static_cast<double>(std::max(0.0f,contributions[i])) : 0.0;
        const double width = octaveWidth * (i == 0 || i+1 == contributions.size() ? .5 : 1.0);
        // The shared contribution grid already contains the bass detail; a full-rate FFT blur would erase it again.
        density[i] = value / (normalising * width);
    }
    return density;
}

inline double displayHeight(double density) {
    density = std::max(0.0,density);
    // Leave headroom for transient changes after normalising the colour mix.
    constexpr double halfHeightDensity = 2.0;
    return density / (halfHeightDensity+density);
}

// https://docs.scipy.org/doc/scipy/reference/generated/scipy.interpolate.BSpline.html
inline double roundedDensityAt(std::span<const double> density, double position) {
    if (density.empty()) return 0.0;
    if (density.size() == 1) return density.front();
    const auto last = static_cast<std::ptrdiff_t>(density.size()-1);
    position = std::isfinite(position) ? std::clamp(position,0.0,static_cast<double>(last)) : 0.0;
    const auto left = std::min(static_cast<std::ptrdiff_t>(position),last-1);
    const double t = position-static_cast<double>(left);
    const double t2 = t*t, t3 = t2*t, remaining = 1.0-t;
    const auto at = [&](std::ptrdiff_t index) {
        if (index < 0) index = -index;
        if (index > last) index = 2*last-index;
        return density[static_cast<std::size_t>(index)];
    };
    return (remaining*remaining*remaining*at(left-1) + (3*t3-6*t2+4)*at(left) +
        (-3*t3+3*t2+3*t+1)*at(left+1) + t3*at(left+2))/6.0;
}

inline std::vector<float> curveHeights(std::span<const float> contributions, std::size_t count) {
    const auto density = contributionDensity(contributions);
    std::vector<float> result(count, 0.0f);
    if (density.size() < 2 || count < 2) return result;
    for (std::size_t i = 0; i < count; ++i) {
        const double position = static_cast<double>(i) * static_cast<double>(density.size()-1) / static_cast<double>(count-1);
        result[i] = static_cast<float>(displayHeight(roundedDensityAt(density,position)));
    }
    return result;
}

}

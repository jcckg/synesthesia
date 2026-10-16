#pragma once

#include <array>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ColourPresentation {

inline constexpr std::size_t contributionBandCount = 241;
inline constexpr double contributionMinimumFrequency = 20.0;
inline constexpr double contributionMaximumFrequency = 20000.0;

inline double contributionBandPosition(double frequency) {
    return std::log(frequency / contributionMinimumFrequency) /
        std::log(contributionMaximumFrequency / contributionMinimumFrequency) * (contributionBandCount - 1);
}

inline float contributionBandFrequency(std::size_t index) {
    return static_cast<float>(contributionMinimumFrequency * std::pow(
        contributionMaximumFrequency / contributionMinimumFrequency,
        static_cast<double>(index) / (contributionBandCount - 1)));
}

struct SpectralContribution {
    std::uint16_t band = 0;
    std::array<float, 3> xyz{};
    float magnitude = 0.0f;
    float responseSeconds = 0.0f;
    bool operator==(const SpectralContribution&) const = default;
};

using SpectralContributions = std::vector<SpectralContribution>;

struct ResponseRange { float minimum = 0.0f; float maximum = 0.0f; };

inline ResponseRange responseRange(const SpectralContributions& contributions) {
    double total = 0.0;
    for (const auto& band : contributions)
        for (const auto value : band.xyz) total += std::abs(static_cast<double>(value));
    ResponseRange result{std::numeric_limits<float>::infinity(), 0.0f};
    for (const auto& band : contributions) {
        const double size = std::abs(static_cast<double>(band.xyz[0])) + std::abs(static_cast<double>(band.xyz[1])) +
            std::abs(static_cast<double>(band.xyz[2]));
        if (size <= total * .0001 || band.responseSeconds <= 0.0f) continue;
        result.minimum = std::min(result.minimum, band.responseSeconds);
        result.maximum = std::max(result.maximum, band.responseSeconds);
    }
    if (!std::isfinite(result.minimum)) return {};
    return result;
}


}

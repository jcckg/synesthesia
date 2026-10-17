#pragma once

#include <array>
#include <limits>
#include "ui/smoothing/smoothing.h"
#include "colour/spectral_contributions.h"

namespace SpectralPresentation {

class ContributionHistory {
public:
    void seed(const ColourPresentation::SpectralContributions& contributions);
    const ColourPresentation::SpectralContributions& process(
        const ColourPresentation::SpectralContributions& targets, double timestamp,
        bool enabled, bool manual, float amount, float frequencyResolution = 0.0f);
    [[nodiscard]] const std::array<float, 3>& xyz() const { return xyz_; }
    [[nodiscard]] float responseSeconds() const { return responseSeconds_; }
    [[nodiscard]] float spectralFlux() const { return spectralFlux_; }
    [[nodiscard]] bool onsetDetected() const { return onsetDetected_; }
private:
    struct Band {
        std::array<double, 3> xyz{};
        std::array<double, 3> derivative{};
        std::array<double, 3> previousTarget{};
        double previousMagnitude = 0.0;
    };
    std::array<Band, ColourPresentation::contributionBandCount> bands_{};
    ColourPresentation::SpectralContributions presented_;
    std::array<float, 3> xyz_{};
    double timestamp_ = std::numeric_limits<double>::quiet_NaN();
    float responseSeconds_ = 0.0f;
    float spectralFlux_ = 0.0f;
    bool onsetDetected_ = false;
    double meanFlux_ = 0.0;
    double varianceFlux_ = 0.0;
    double previousFlux_ = 0.0;
    double precedingFlux_ = 0.0;
    double previousThreshold_ = 0.0;
    bool seeded_ = false;
    bool manual_ = false;
};

}

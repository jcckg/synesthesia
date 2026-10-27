#include "contribution_history.h"

#include <algorithm>
#include <numbers>
#include "colour/colour_core.h"

namespace SpectralPresentation {

void ContributionHistory::seed(const ColourPresentation::SpectralContributions& contributions) {
    *this = {};
    for (const auto& contribution : contributions) {
        if (contribution.band >= bands_.size()) continue;
        for (std::size_t c = 0; c < 3; ++c)
            if (std::isfinite(contribution.xyz[c])) bands_[contribution.band].xyz[c] += static_cast<double>(contribution.xyz[c]);
    }
    seeded_ = true;
}

// https://librosa.org/doc/0.11.0/generated/librosa.onset.onset_strength.html
// https://gery.casiez.net/publications/CHI2012-casiez.pdf
const ColourPresentation::SpectralContributions& ContributionHistory::process(
    const ColourPresentation::SpectralContributions& targets, double timestamp,
    bool enabled, bool manual, float amount, float frequencyResolution) {
    if (!std::isfinite(timestamp) || timestamp == timestamp_) return presented_;
    if (timestamp < timestamp_) *this = {};
    const bool initial = !std::isfinite(timestamp_);
    const bool primeManual = initial || manual != manual_;
    manual_ = manual;
    const double dt = initial ? 0.0 : timestamp - timestamp_;
    constexpr auto count = ColourPresentation::contributionBandCount;
    static const auto frequencies = [] {
        std::array<double, count> values{};
        for (std::size_t i = 0; i < count; ++i) values[i] = static_cast<double>(ColourPresentation::contributionBandFrequency(i));
        return values;
    }();
    std::array<std::array<double, 3>, count> targetXYZ{};
    std::array<double, count> magnitudes{};
    for (const auto& target : targets) {
        if (target.band >= count) continue;
        for (std::size_t c = 0; c < 3; ++c)
            if (std::isfinite(target.xyz[c])) targetXYZ[target.band][c] += static_cast<double>(target.xyz[c]);
        if (std::isfinite(target.magnitude)) magnitudes[target.band] += static_cast<double>(std::max(0.0f, target.magnitude));
    }
    frequencyResolution = std::isfinite(frequencyResolution) ? std::max(0.0f, frequencyResolution) : 0.0f;
    const double analysisDuration = frequencyResolution > 0.0f ? 1.0 / static_cast<double>(frequencyResolution) : std::max(dt, 1.0 / 60.0);
    const double response95 = std::log(20.0);
    const double derivativeWeight = -std::expm1(-2.0 * std::numbers::pi * dt);
    const double base = static_cast<double>(manualResponseTime(amount));
    std::array<double, 3> combined{};
    double weightedResponse = 0.0, presence = 0.0, positiveFlux = 0.0, totalMagnitude = 0.0;
    presented_.clear();
    for (std::size_t i = 0; i < count; ++i) {
        auto& band = bands_[i];
        positiveFlux += std::max(0.0, magnitudes[i] - band.previousMagnitude);
        totalMagnitude += std::max(magnitudes[i], band.previousMagnitude);
        std::array<double, 3> local{};
        const double frequency = frequencies[i];
        const double cellWidth = frequency * std::expm1(std::log(1000.0) / (count - 1));
        const double radius = std::max(2.0 * static_cast<double>(frequencyResolution), cellWidth);
        // A bounded Hann main-lobe neighbourhood cannot form transitive broadband groups.
        for (std::size_t j = i; j < count; ++j) {
            const double distance = frequencies[j] - frequency;
            if (distance >= 2.0 * radius && j != i) break;
            const double weight = std::max(0.0, 1.0 - distance / radius);
            for (std::size_t c = 0; c < 3; ++c) local[c] += weight * targetXYZ[j][c];
        }
        for (std::size_t j = i; j > 0;) {
            --j;
            const double distance = frequency - frequencies[j];
            if (distance >= 2.0 * radius) break;
            const double weight = std::max(0.0, 1.0 - distance / radius);
            for (std::size_t c = 0; c < 3; ++c) local[c] += weight * targetXYZ[j][c];
        }
        double squaredSpeed = 0.0;
        if (manual) {
            std::array<float, 3> perceptual{};
            ColourCore::XYZtoOklab(static_cast<float>(local[0]), static_cast<float>(local[1]),
                static_cast<float>(local[2]), perceptual[0], perceptual[1], perceptual[2]);
            for (std::size_t c = 0; c < 3; ++c) {
                const double coordinate = static_cast<double>(perceptual[c]) / 100.0;
                const double change = !primeManual && dt > 0.0 ? (coordinate - band.previousTarget[c]) / dt : 0.0;
                if (primeManual) band.derivative[c] = 0.0;
                band.derivative[c] += derivativeWeight * (change - band.derivative[c]);
                squaredSpeed += band.derivative[c] * band.derivative[c];
                band.previousTarget[c] = coordinate;
            }
        }
        const double rate = manual ? std::min(response95 / .04,
            response95 / base + 2.0 * std::numbers::pi * 4.0 * std::sqrt(squaredSpeed)) : std::numeric_limits<double>::infinity();
        const double retained = dt > 0.0 ? std::exp(-rate * dt) : 1.0;
        const double response = retained > 0.0 && retained < 1.0 ? -response95 * dt / std::log(retained) : 0.0;
        double size = 0.0;
        for (std::size_t c = 0; c < 3; ++c) {
            if (!enabled || !manual || (initial && !seeded_)) band.xyz[c] = targetXYZ[i][c];
            else band.xyz[c] = std::lerp(targetXYZ[i][c], band.xyz[c], retained);
            combined[c] += band.xyz[c];
            size += std::abs(band.xyz[c]);
        }
        if (size > 0.0 || magnitudes[i] > 0.0) {
            ColourPresentation::SpectralContribution output;
            output.band = static_cast<std::uint16_t>(i);
            for (std::size_t c = 0; c < 3; ++c) output.xyz[c] = static_cast<float>(band.xyz[c]);
            output.magnitude = static_cast<float>(magnitudes[i]);
            output.responseSeconds = enabled ? static_cast<float>(response) : 0.0f;
            presented_.push_back(output);
        }
        weightedResponse += size * response;
        presence += size;
    }
    for (std::size_t i = 0; i < count; ++i) bands_[i].previousMagnitude = magnitudes[i];
    for (std::size_t c = 0; c < 3; ++c) xyz_[c] = static_cast<float>(combined[c]);
    const double flux = !initial && totalMagnitude > 0.0 ? std::clamp(positiveFlux / totalMagnitude * analysisDuration / dt, 0.0, 1.0) : 0.0;
    onsetDetected_ = previousFlux_ > precedingFlux_ && previousFlux_ >= flux && previousFlux_ > previousThreshold_;
    const double weight = -std::expm1(-dt / 1.5);
    const double difference = flux - meanFlux_;
    meanFlux_ += weight * difference;
    varianceFlux_ = (1.0 - weight) * (varianceFlux_ + weight * difference * difference);
    previousThreshold_ = meanFlux_ + std::sqrt(varianceFlux_);
    precedingFlux_ = previousFlux_;
    previousFlux_ = flux;
    spectralFlux_ = static_cast<float>(flux);
    responseSeconds_ = enabled && presence > 0.0 ? static_cast<float>(weightedResponse / presence) : 0.0f;
    timestamp_ = timestamp;
    return presented_;
}

}

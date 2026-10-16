#include "contribution_history.h"

#include <algorithm>
#include <numbers>
#include <vector>
#include "colour/colour_core.h"

namespace SpectralPresentation {

void ContributionHistory::seed(const ColourPresentation::SpectralContributions& contributions) {
    *this = {};
    for (const auto& contribution : contributions) {
        if (contribution.band >= bands_.size()) continue;
        auto& band = bands_[contribution.band];
        for (std::size_t c = 0; c < 3; ++c) band.xyz[c] = contribution.xyz[c];
        band.presence = std::max(0.0f, contribution.magnitude);
        for (const auto component : band.xyz) band.presence = std::max(band.presence, std::abs(component));
    }
    seeded_ = true;
}

// https://gery.casiez.net/publications/CHI2012-casiez.pdf
// https://www.dsprelated.com/freebooks/sasp/Spectrum_Analysis_Sinusoids.html
const ColourPresentation::SpectralContributions& ContributionHistory::process(
    const ColourPresentation::SpectralContributions& targets, double timestamp,
    bool enabled, bool manual, float amount, float flatness, float frequencyResolution) {
    if (!std::isfinite(timestamp) || timestamp == timestamp_) return presented_;
    if (timestamp < timestamp_) *this = {};
    const bool initial = !std::isfinite(timestamp_);
    const bool primeManual = initial || manual != manual_;
    manual_ = manual;
    const double dt = initial ? 0.0 : timestamp - timestamp_;
    constexpr auto count = ColourPresentation::contributionBandCount;
    std::array<std::array<double, 3>, count> targetXYZ{};
    std::array<double, count> magnitudes{};
    for (const auto& target : targets) {
        if (target.band >= count) continue;
        for (std::size_t c = 0; c < 3; ++c)
            if (std::isfinite(target.xyz[c])) targetXYZ[target.band][c] += target.xyz[c];
        if (std::isfinite(target.magnitude)) magnitudes[target.band] += std::max(0.0f, target.magnitude);
    }
    const double maximum = *std::max_element(magnitudes.begin(), magnitudes.end());
    // Expire weak bridges relative to the current signal so old leakage cannot keep unrelated groups coupled.
    const double significance = std::max(1e-12, maximum * 1e-3);
    std::array<bool, count> occupied{};
    std::array<double, count> precedingPresence{};
    for (std::size_t i = 0; i < count; ++i) {
        precedingPresence[i] = bands_[i].presence;
        const double current = magnitudes[i] >= significance ? magnitudes[i] : 0.0;
        bands_[i].presence = std::max(current, bands_[i].presence * std::exp(-dt / .5));
        occupied[i] = bands_[i].presence >= significance;
    }
    if (std::isfinite(frequencyResolution) && frequencyResolution > 0) {
        // Neighbouring FFT bins can occupy separated logarithmic cells; connect their frequency footprints.
        std::size_t previous = count;
        for (std::size_t i = 0; i < count; ++i) if (occupied[i]) {
            if (previous < count && i > previous+1 &&
                ColourPresentation::contributionBandFrequency(i)-
                ColourPresentation::contributionBandFrequency(previous) <= 1.5*frequencyResolution)
                std::fill(occupied.begin()+static_cast<std::ptrdiff_t>(previous+1),occupied.begin()+static_cast<std::ptrdiff_t>(i),true);
            previous = i;
        }
    }
    struct Group { std::size_t first; std::size_t last; std::shared_ptr<Envelope> envelope; };
    std::vector<Group> groups;
    std::vector<std::shared_ptr<Envelope>> used;
    for (std::size_t first = 0; first < count;) {
        std::size_t last = first;
        if (occupied[first]) while (last + 1 < count && occupied[last + 1]) ++last;
        std::shared_ptr<Envelope> envelope;
        double bestPresence = -1.0;
        for (std::size_t i = first; i <= last; ++i) {
            const auto& candidate = bands_[i].envelope;
            if (candidate && precedingPresence[i] > bestPresence) {
                envelope = candidate;
                bestPresence = precedingPresence[i];
            }
        }
        // A split inherits response history instead of beginning another attack from silence.
        if (envelope && std::find(used.begin(),used.end(),envelope) != used.end())
            envelope = std::make_shared<Envelope>(*envelope);
        if (!envelope) {
            envelope = std::make_shared<Envelope>();
            if (!initial) envelope->observations.emplace_back(timestamp - .032, -140.0);
        }
        used.push_back(envelope);
        groups.push_back({first, last, envelope});
        first = last + 1;
    }
    onsetDetected_ = false;
    spectralFlux_ = 0.0f;
    const double derivativeWeight = -std::expm1(-2.0 * std::numbers::pi * dt);
    const double response95 = -std::log(.05);
    const double base = manual ? manualResponseTime(amount) :
        std::lerp(.4, .75, std::isfinite(flatness) ? std::clamp(static_cast<double>(flatness), 0.0, 1.0) : 0.0);
    presented_.clear();
    std::array<double, 3> combined{};
    double weightedResponse = 0.0, presence = 0.0, weightedFlux = 0.0, attackingPresence = 0.0, totalPresence = 0.0;
    for (const auto& group : groups) {
        auto& envelope = *group.envelope;
        double magnitude = 0.0, previousMagnitude = 0.0;
        std::array<double, 3> aggregate{};
        for (std::size_t i = group.first; i <= group.last; ++i) {
            magnitude += magnitudes[i];
            previousMagnitude += bands_[i].previousMagnitude;
            for (std::size_t c = 0; c < 3; ++c) aggregate[c] += targetXYZ[i][c];
        }
        if (previousMagnitude != envelope.magnitude && previousMagnitude > 1e-7 && envelope.magnitude > 1e-7) {
            // Rebase detector levels when membership changes so regrouping is not mistaken for an audio attack.
            const double shift = 20*std::log10(previousMagnitude/envelope.magnitude);
            for (auto& observation : envelope.observations) observation.second += shift;
            envelope.response.shiftLevel(shift);
        }
        envelope.magnitude = magnitude;
        const double level = 20.0 * std::log10(std::max(magnitude, 1e-7));
        const double referenceTime = timestamp - .032;
        auto& observations = envelope.observations;
        while (observations.size() > 1 && observations[1].first <= referenceTime) observations.pop_front();
        double rise = 0.0;
        if (!observations.empty() && observations.front().first <= referenceTime) {
            const auto first = observations.front();
            const auto second = observations.size() > 1 ? observations[1] : first;
            const double fraction = second.first > first.first ?
                std::clamp((referenceTime - first.first) / (second.first - first.first), 0.0, 1.0) : 0.0;
            rise = level - std::lerp(first.second, second.second, fraction);
        }
        const double flux = level > -70.0 ? std::clamp(rise / 12.0, 0.0, 1.0) : 0.0;
        SmoothingSignalFeatures features;
        features.frameLevelDb = static_cast<float>(level);
        features.audioTimeSeconds = timestamp;
        features.spectralFlatness = flatness;
        features.spectralFlux = static_cast<float>(flux);
        features.onsetDetected = envelope.previousFlux > envelope.precedingFlux &&
            envelope.previousFlux >= flux && envelope.previousFlux > envelope.previousThreshold;
        const auto automaticResponse = envelope.response.update(features, rise > 3.0 && flux > envelope.previousThreshold);
        envelope.meanFlux += -std::expm1(-dt / .35) * (flux - envelope.meanFlux);
        envelope.previousThreshold = std::max(.25, envelope.meanFlux + .1);
        envelope.precedingFlux = envelope.previousFlux;
        envelope.previousFlux = flux;
        observations.emplace_back(timestamp, level);
        double squaredSpeed = 0.0;
        if (manual) {
            std::array<float, 3> perceptual{};
            ColourCore::XYZtoOklab(static_cast<float>(aggregate[0]), static_cast<float>(aggregate[1]),
                static_cast<float>(aggregate[2]), perceptual[0], perceptual[1], perceptual[2]);
            for (std::size_t c = 0; c < 3; ++c) {
                const double coordinate = perceptual[c] / 100.0;
                const double change = !primeManual && dt > 0.0 ? (coordinate - envelope.previousTarget[c]) / dt : 0.0;
                if (primeManual) envelope.derivative[c] = 0.0;
                envelope.derivative[c] += derivativeWeight * (change - envelope.derivative[c]);
                squaredSpeed += envelope.derivative[c] * envelope.derivative[c];
                envelope.previousTarget[c] = coordinate;
            }
        }
        const double pole = manual ? std::min(response95 / .04,
            response95 / base + 2.0 * std::numbers::pi * 4.0 * std::sqrt(squaredSpeed)) :
            response95 / automaticResponse;
        const double weight = -std::expm1(-pole * dt);
        for (std::size_t i = group.first; i <= group.last; ++i) {
            auto& band = bands_[i];
            band.envelope = group.envelope;
            band.previousMagnitude = magnitudes[i];
            double size = 0.0, rawPresence = 0.0;
            for (std::size_t c = 0; c < 3; ++c) {
                if (!enabled || (initial && !seeded_)) band.xyz[c] = targetXYZ[i][c];
                else band.xyz[c] += weight * (targetXYZ[i][c] - band.xyz[c]);
                combined[c] += band.xyz[c];
                size += std::abs(band.xyz[c]);
                rawPresence += std::abs(targetXYZ[i][c]);
            }
            if (size > 0.0 || magnitudes[i] > 0.0) {
                ColourPresentation::SpectralContribution output;
                output.band = static_cast<std::uint16_t>(i);
                for (std::size_t c = 0; c < 3; ++c) output.xyz[c] = static_cast<float>(band.xyz[c]);
                output.magnitude = static_cast<float>(magnitudes[i]);
                output.responseSeconds = enabled ? static_cast<float>(response95 / pole) : 0.0f;
                presented_.push_back(output);
            }
            if (envelope.response.attackDetected()) attackingPresence += rawPresence;
            totalPresence += rawPresence;
            weightedFlux += rawPresence * flux;
            weightedResponse += size * response95 / pole;
            presence += size;
        }
    }
    for (std::size_t c = 0; c < 3; ++c) xyz_[c] = static_cast<float>(combined[c]);
    const double flux = totalPresence > 0.0 ? weightedFlux / totalPresence : 0.0;
    if (totalPresence > 0.0 && attackingPresence >= .05 * totalPresence) lastLocalAttack_ = timestamp;
    onsetDetected_ = previousFlux_ > precedingFlux_ && previousFlux_ >= flux && previousFlux_ > previousThreshold_ &&
        timestamp - lastLocalAttack_ <= .064 + 1e-9 && timestamp - lastOnset_ >= .08 - 1e-9;
    if (onsetDetected_) lastOnset_ = timestamp;
    meanFlux_ += -std::expm1(-dt / .35) * (flux - meanFlux_);
    previousThreshold_ = std::max(.04, meanFlux_ + .04);
    precedingFlux_ = previousFlux_;
    previousFlux_ = flux;
    spectralFlux_ = static_cast<float>(flux);
    responseSeconds_ = enabled ? static_cast<float>(presence > 0.0 ? weightedResponse / presence : base) : 0.0f;
    timestamp_ = timestamp;
    return presented_;
}

}

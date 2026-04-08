#include "audio/analysis/phase/phase_features.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "audio/analysis/presentation/spectral_presentation.h"

namespace PhaseAnalysis {

namespace {

constexpr float kMinimumMagnitude = 1e-5f;
constexpr float kRelativeMagnitudeFloor = 0.025f;
constexpr float kMinimumDeltaTime = 1e-4f;
constexpr float kMaximumDeltaTime = 0.25f;
constexpr float kMinimumAudibleFrequency = 20.0f;
constexpr float kMaximumAudibleFrequency = 20000.0f;

float wrapPhase(const float value) {
    return std::atan2(std::sin(value), std::cos(value));
}

std::span<const float> resolveFrequencies(std::vector<float>& buffer,
                                          std::span<const float> frequencies,
                                          const size_t binCount,
                                          const float sampleRate) {
    if (frequencies.size() == binCount) {
        return frequencies;
    }

    buffer.assign(binCount, 0.0f);
    if (binCount <= 1 || sampleRate <= 0.0f) {
        return std::span<const float>(buffer.data(), buffer.size());
    }

    const float binWidth = sampleRate / (2.0f * static_cast<float>(binCount - 1));
    for (size_t index = 0; index < binCount; ++index) {
        buffer[index] = static_cast<float>(index) * binWidth;
    }

    return std::span<const float>(buffer.data(), buffer.size());
}

SpectralPresentation::Frame mixSampleFrame(const AudioColourSample& sample) {
    return SpectralPresentation::mixChannels(
        sample.magnitudes,
        sample.phases,
        sample.frequencies,
        sample.channels,
        sample.sampleRate);
}

} // namespace

bool PhaseFeatureMetrics::isNeutral() const {
    return instabilityNorm <= 1e-6f &&
        coherenceNorm <= 1e-6f &&
        transientNorm <= 1e-6f;
}

PhaseFeatureMetrics analyseTransition(std::span<const float> previousMagnitudes,
                                      std::span<const float> previousPhases,
                                      std::span<const float> currentMagnitudes,
                                      std::span<const float> currentPhases,
                                      std::span<const float> frequencies,
                                      const float sampleRate,
                                      const float deltaTimeSeconds) {
    PhaseFeatureMetrics metrics{};

    const size_t binCount = std::min({
        previousMagnitudes.size(),
        previousPhases.size(),
        currentMagnitudes.size(),
        currentPhases.size()
    });
    if (binCount <= 1) {
        return metrics;
    }

    const float clampedDeltaTime = std::clamp(deltaTimeSeconds, kMinimumDeltaTime, kMaximumDeltaTime);
    if (!std::isfinite(clampedDeltaTime) || sampleRate <= 0.0f) {
        return metrics;
    }

    std::vector<float> derivedFrequencies;
    const std::span<const float> effectiveFrequencies = resolveFrequencies(
        derivedFrequencies,
        frequencies,
        binCount,
        sampleRate);

    float peakWeight = 0.0f;
    for (size_t index = 1; index < binCount; ++index) {
        const float previousMagnitude = previousMagnitudes[index];
        const float currentMagnitude = currentMagnitudes[index];
        if (!std::isfinite(previousMagnitude) || !std::isfinite(currentMagnitude)) {
            continue;
        }

        peakWeight = std::max(peakWeight, 0.5f * (std::max(previousMagnitude, 0.0f) + std::max(currentMagnitude, 0.0f)));
    }

    const float minimumWeight = std::max(kMinimumMagnitude, peakWeight * kRelativeMagnitudeFloor);
    const float phaseScale = 1.0f / std::numbers::pi_v<float>;

    float totalWeight = 0.0f;
    float highFrequencyWeight = 0.0f;
    float instabilityAccumulator = 0.0f;
    float transientAccumulator = 0.0f;
    float cosAccumulator = 0.0f;
    float sinAccumulator = 0.0f;

    for (size_t index = 1; index < binCount; ++index) {
        const float frequency = effectiveFrequencies[index];
        if (!std::isfinite(frequency) ||
            frequency < kMinimumAudibleFrequency ||
            frequency > kMaximumAudibleFrequency) {
            continue;
        }

        const float previousMagnitude = previousMagnitudes[index];
        const float currentMagnitude = currentMagnitudes[index];
        const float previousPhase = previousPhases[index];
        const float currentPhase = currentPhases[index];
        if (!std::isfinite(previousMagnitude) ||
            !std::isfinite(currentMagnitude) ||
            !std::isfinite(previousPhase) ||
            !std::isfinite(currentPhase)) {
            continue;
        }

        const float weight = 0.5f * (std::max(previousMagnitude, 0.0f) + std::max(currentMagnitude, 0.0f));
        if (weight < minimumWeight) {
            continue;
        }

        const float expectedAdvance = 2.0f * std::numbers::pi_v<float> * frequency * clampedDeltaTime;
        const float residual = wrapPhase(currentPhase - previousPhase - expectedAdvance);
        const float absoluteResidual = std::abs(residual);
        const float frequencyNorm = std::clamp(frequency / std::max(sampleRate * 0.5f, 1.0f), 0.0f, 1.0f);
        const float transientWeight = weight * (0.35f + 0.65f * frequencyNorm);

        totalWeight += weight;
        highFrequencyWeight += transientWeight;
        instabilityAccumulator += weight * absoluteResidual;
        transientAccumulator += transientWeight * absoluteResidual;
        cosAccumulator += weight * std::cos(residual);
        sinAccumulator += weight * std::sin(residual);
    }

    if (totalWeight <= 1e-6f) {
        return metrics;
    }

    metrics.instabilityNorm = std::clamp(instabilityAccumulator * phaseScale / totalWeight, 0.0f, 1.0f);
    metrics.coherenceNorm = std::clamp(
        std::sqrt(cosAccumulator * cosAccumulator + sinAccumulator * sinAccumulator) / totalWeight,
        0.0f,
        1.0f);

    if (highFrequencyWeight > 1e-6f) {
        metrics.transientNorm = std::clamp(transientAccumulator * phaseScale / highFrequencyWeight, 0.0f, 1.0f);
    }

    return metrics;
}

PhaseFeatureMetrics analyseTransition(const SpectralPresentation::Frame* previousFrame,
                                      const SpectralPresentation::Frame& currentFrame,
                                      const float deltaTimeSeconds) {
    if (previousFrame == nullptr) {
        return {};
    }

    return analyseTransition(
        std::span<const float>(previousFrame->magnitudes.data(), previousFrame->magnitudes.size()),
        std::span<const float>(previousFrame->phases.data(), previousFrame->phases.size()),
        std::span<const float>(currentFrame.magnitudes.data(), currentFrame.magnitudes.size()),
        std::span<const float>(currentFrame.phases.data(), currentFrame.phases.size()),
        std::span<const float>(currentFrame.frequencies.data(), currentFrame.frequencies.size()),
        currentFrame.sampleRate,
        deltaTimeSeconds);
}

PhaseFeatureMetrics analyseTransition(const AudioColourSample* previousSample,
                                      const AudioColourSample& currentSample) {
    if (previousSample == nullptr) {
        return {};
    }

    const double deltaTime = currentSample.timestamp - previousSample->timestamp;
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0) {
        return {};
    }

    const SpectralPresentation::Frame previousFrame = mixSampleFrame(*previousSample);
    const SpectralPresentation::Frame currentFrame = mixSampleFrame(currentSample);
    return analyseTransition(&previousFrame, currentFrame, static_cast<float>(deltaTime));
}

} // namespace PhaseAnalysis

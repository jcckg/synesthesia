#include "ui/smoothing/smoothing_features.h"

#include <algorithm>
#include <cmath>

namespace UI::Smoothing {

namespace {

constexpr float kMinCentroid = 100.0f;
constexpr float kMinRolloff = 20.0f;
constexpr float kRolloffLogMin = 4.32f;
constexpr float kRolloffLogMax = 14.29f;
constexpr float kCrestLogScale = 4.0f;

}

SmoothingSignalFeatures buildSignalFeatures(const ColourCore::FrameResult& result) {
    SmoothingSignalFeatures features{};
    features.spectralFlatness = result.spectralFlatness;
    features.loudnessNormalised = std::clamp(result.loudnessNormalised, 0.0f, 1.0f);
    features.brightnessNormalised = std::clamp(result.brightnessNormalised, 0.0f, 1.0f);

    const float centroid = std::max(result.spectralCentroid, kMinCentroid);
    features.spectralSpreadNorm = std::clamp(result.spectralSpread / centroid * 0.5f, 0.0f, 1.0f);

    const float rolloffLog = std::log2(std::max(result.spectralRolloff, kMinRolloff));
    features.spectralRolloffNorm = std::clamp(
        (rolloffLog - kRolloffLogMin) / (kRolloffLogMax - kRolloffLogMin),
        0.0f,
        1.0f);
    features.spectralCrestNorm = std::clamp(
        std::log2(std::max(result.spectralCrestFactor, 1.0f)) / kCrestLogScale,
        0.0f,
        1.0f);

    features.phaseInstabilityNorm = std::clamp(result.phaseInstabilityNorm, 0.0f, 1.0f);
    features.phaseCoherenceNorm = std::clamp(result.phaseCoherenceNorm, 0.0f, 1.0f);
    features.phaseTransientNorm = std::clamp(result.phaseTransientNorm, 0.0f, 1.0f);
    return features;
}

}

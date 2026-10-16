#pragma once

#include <span>
#include <cstdint>
#include <vector>

#include "resyne/encoding/formats/exporter.h"

namespace SpectralPresentation {
struct Frame;
}

namespace PhaseAnalysis {

struct PhaseFeatureMetrics {
    float instabilityNorm = 0.0f;
    float coherenceNorm = 0.0f;
    float transientNorm = 0.0f;

    [[nodiscard]] bool isNeutral() const;
};

class LiveFrameTracker {
public:
    PhaseFeatureMetrics update(const SpectralPresentation::Frame& frame, std::uint64_t counter, int hopSize);
    void reset();
private:
    std::vector<float> magnitudes, phases;
    std::uint64_t previousCounter = 0;
    float previousRate = 0.0f;
    int previousHop = 0;
    bool available = false;
    PhaseFeatureMetrics cached{};
};

PhaseFeatureMetrics analyseTransition(std::span<const float> previousMagnitudes,
                                      std::span<const float> previousPhases,
                                      std::span<const float> currentMagnitudes,
                                      std::span<const float> currentPhases,
                                      std::span<const float> frequencies,
                                      float sampleRate,
                                      float deltaTimeSeconds);

PhaseFeatureMetrics analyseTransition(const SpectralPresentation::Frame* previousFrame,
                                      const SpectralPresentation::Frame& currentFrame,
                                      float deltaTimeSeconds);

PhaseFeatureMetrics analyseTransition(const AudioColourSample* previousSample,
                                      const AudioColourSample& currentSample);

}

#include "phase_smoothing.h"
#include "phase_wrapping.h"

#include <cmath>
#include <numbers>
#include <vector>

namespace PhaseReconstruction {

namespace {
constexpr float MIN_BIN_INTENSITY = 1e-6f;
constexpr float MOMENTUM = 0.95f;
constexpr float SMOOTHING = 0.7f;
}

void smoothPhase(std::vector<float>& phases,
				 const std::vector<float>& targetMagnitudes,
				 size_t iterations) {
	if (phases.size() != targetMagnitudes.size() || phases.empty()) {
		return;
	}

	for (size_t iter = 0; iter < iterations; ++iter) {
		std::vector<float> newPhase = phases;

		for (size_t bin = 1; bin < phases.size() - 1; ++bin) {
			if (targetMagnitudes[bin] <= MIN_BIN_INTENSITY) {
				continue;
			}

			float phaseSum = 0.0f;
			float weightSum = 0.0f;

			if (targetMagnitudes[bin - 1] > MIN_BIN_INTENSITY) {
				const float weight = targetMagnitudes[bin - 1];
				phaseSum += phases[bin - 1] * weight;
				weightSum += weight;
			}

			if (targetMagnitudes[bin + 1] > MIN_BIN_INTENSITY) {
				const float weight = targetMagnitudes[bin + 1];
				phaseSum += phases[bin + 1] * weight;
				weightSum += weight;
			}

			const float centreWeight = targetMagnitudes[bin] * 2.0f;
			phaseSum += phases[bin] * centreWeight;
			weightSum += centreWeight;

			if (weightSum > 0.0f) {
				const float smoothedPhase = phaseSum / weightSum;
				newPhase[bin] = wrapToPi(SMOOTHING * smoothedPhase + (1.0f - SMOOTHING) * phases[bin]);
			}
		}

		for (size_t bin = 0; bin < phases.size(); ++bin) {
			const float delta = wrapToPi(newPhase[bin] - phases[bin]);
			phases[bin] = wrapToPi(phases[bin] + delta * (1.0f + MOMENTUM));
		}
	}

	for (size_t bin = 1; bin < phases.size(); ++bin) {
		if (targetMagnitudes[bin] > MIN_BIN_INTENSITY && targetMagnitudes[bin - 1] > MIN_BIN_INTENSITY) {
			const float phaseDiff = wrapToPi(phases[bin] - phases[bin - 1]);
			if (std::abs(phaseDiff) > std::numbers::pi_v<float> * 0.9f) {
				phases[bin] = wrapToPi(phases[bin - 1] + std::numbers::pi_v<float> * 0.5f);
			}
		}
	}
}

}

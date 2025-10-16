#include "phase_vocoder.h"
#include "phase_wrapping.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace PhaseReconstruction {

namespace {
constexpr float EPSILON = 1e-6f;
constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
}

void alignReconstructedPhase(std::vector<float>& reconPhases,
					        const std::vector<float>& prevOutputPhase,
					        const std::vector<float>& frequencies,
					        const std::vector<float>& damageWeights,
					        float sampleRate,
					        int hopSize) {
	if (reconPhases.size() != prevOutputPhase.size() ||
		reconPhases.size() != damageWeights.size()) {
		return;
	}

	const size_t numBins = reconPhases.size();
	const float fftSize = numBins > 1 ? static_cast<float>((numBins - 1) * 2) : 2.0f;
	const float hop = static_cast<float>(hopSize);

	if (sampleRate <= EPSILON || hopSize <= 0 || fftSize <= 0.0f) {
		return;
	}

	for (size_t bin = 0; bin < numBins; ++bin) {
		const float weight = std::clamp(damageWeights[bin], 0.0f, 1.0f);
		if (weight <= 0.0f) {
			continue;
		}

		float frequency = 0.0f;
		if (bin < frequencies.size() && std::isfinite(frequencies[bin]) && frequencies[bin] > 0.0f) {
			frequency = frequencies[bin];
		} else {
			frequency = sampleRate / fftSize * static_cast<float>(bin);
		}

		const float expectedAdvance = TWO_PI * frequency * hop / sampleRate;
		const float expectedPhase = wrapToPi(prevOutputPhase[bin] + expectedAdvance);
		const float delta = wrapToPi(reconPhases[bin] - expectedPhase);
		reconPhases[bin] = wrapToPi(expectedPhase + delta * weight);
	}
}

}

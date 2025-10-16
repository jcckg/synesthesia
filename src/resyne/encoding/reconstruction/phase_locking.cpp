#include "phase_locking.h"
#include "phase_wrapping.h"

#include <algorithm>

namespace PhaseReconstruction {

namespace {
constexpr float MIN_BIN_INTENSITY = 1e-6f;
constexpr size_t REGION_RADIUS = 4;
}

void applyPhaseLocking(std::vector<float>& phases,
				       const std::vector<float>& magnitudes,
				       const std::vector<size_t>& peaks,
				       const std::vector<float>& damageWeights) {
	if (phases.size() != damageWeights.size()) {
		return;
	}

	for (size_t peakBin : peaks) {
		if (peakBin >= phases.size()) {
			continue;
		}
		if (magnitudes[peakBin] <= MIN_BIN_INTENSITY || damageWeights[peakBin] <= 0.0f) {
			continue;
		}

		const float peakPhase = phases[peakBin];
		const size_t startBin = peakBin > REGION_RADIUS ? peakBin - REGION_RADIUS : 0;
		const size_t endBin = std::min(peakBin + REGION_RADIUS + 1, phases.size());

		for (size_t k = startBin; k < endBin; ++k) {
			if (k == peakBin || magnitudes[k] <= MIN_BIN_INTENSITY) {
				continue;
			}

			const float blend = std::clamp(damageWeights[k], 0.0f, 1.0f);
			if (blend <= 0.0f) {
				continue;
			}

			const float magnitudeRatio = magnitudes[k] / magnitudes[peakBin];
			const float strength = std::clamp(magnitudeRatio * blend, 0.0f, 1.0f);

			phases[k] = wrapToPi(strength * phases[k] + (1.0f - strength) * peakPhase);
		}
	}
}

}

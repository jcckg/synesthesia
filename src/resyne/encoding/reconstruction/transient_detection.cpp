#include "transient_detection.h"

#include <algorithm>

namespace PhaseReconstruction {

float computeSpectralFlux(const std::vector<float>& currentMag,
						  const std::vector<float>& previousMag) {
	float flux = 0.0f;
	const size_t minSize = std::min(currentMag.size(), previousMag.size());

	for (size_t i = 0; i < minSize; ++i) {
		const float diff = currentMag[i] - previousMag[i];
		if (diff > 0.0f) {
			flux += diff;
		}
	}

	return flux;
}

}

#pragma once

#include <cstddef>
#include <vector>

namespace PhaseReconstruction {

void interpolateBoundaryPhase(std::vector<float>& phases,
							  const std::vector<float>& originalPhases,
							  const std::vector<float>& reconstructedPhases,
							  const std::vector<float>& transitionWeights,
							  size_t binCount);

void slerpPhaseVectors(float cosOriginal, float sinOriginal,
					   float cosReconstructed, float sinReconstructed,
					   float weight,
					   float& cosResult, float& sinResult);

void applyTemporalPhaseCoherence(std::vector<std::vector<float>>& allPhases,
								 const std::vector<float>& transitionWeights,
								 size_t width,
								 size_t height,
								 float coherenceFactor);

}

#pragma once

#include <cstddef>
#include <vector>

namespace PhaseReconstruction {

void interpolateBoundaryPhase(std::vector<float>& phases,
							  const std::vector<float>& originalPhases,
							  const std::vector<float>& reconstructedPhases,
							  const std::vector<float>& transitionWeights,
							  size_t binCount);



void applyTemporalPhaseCoherence(std::vector<std::vector<float>>& allPhases,
								 const std::vector<float>& transitionWeights,
								 size_t width,
								 size_t height,
								 float coherenceFactor);

}

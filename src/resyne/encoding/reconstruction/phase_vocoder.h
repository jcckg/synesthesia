#pragma once

#include <vector>

namespace PhaseReconstruction {

// Advance the previous phase by one hop, then blend towards the reconstructed phase using damage weights.
void alignReconstructedPhase(std::vector<float>& reconPhases,
					        const std::vector<float>& prevOutputPhase,
					        const std::vector<float>& frequencies,
					        const std::vector<float>& damageWeights,
					        float sampleRate,
					        int hopSize);

}

#pragma once

#include <vector>

namespace PhaseReconstruction {

// Aligns reconstructed phases to previous output phases for temporal continuity
// Laroche & Dolson (1999) - phase vocoder continuity heuristics
void alignReconstructedPhase(std::vector<float>& reconPhases,
					        const std::vector<float>& prevOutputPhase,
					        const std::vector<float>& frequencies,
					        const std::vector<float>& damageWeights,
					        float sampleRate,
					        int hopSize);

}

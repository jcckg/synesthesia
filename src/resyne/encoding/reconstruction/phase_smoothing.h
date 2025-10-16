#pragma once

#include <vector>
#include <cstddef>

namespace PhaseReconstruction {

// Iterative phase smoothing using weighted neighbour averaging
// Inspired by Griffin-Lim (1984) but simplified to magnitude-weighted smoothing
// rather than full STFT-ISTFT alternating projections
void smoothPhase(std::vector<float>& phases,
				 const std::vector<float>& targetMagnitudes,
				 size_t iterations = 3);

}

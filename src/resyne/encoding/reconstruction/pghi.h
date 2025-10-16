#pragma once

#include <vector>
#include <cstddef>

namespace PhaseReconstruction {

// Phase Gradient Heap Integration (PGHI) algorithm
// Průša et al. (2017) - "A noniterative method for reconstruction of phase from STFT magnitude"
// Reconstructs phase from magnitude spectrogram using heap-based integration
void reconstructPhasePGHI(const std::vector<std::vector<float>>& allMagnitudes,
					  const std::vector<std::vector<float>>& allFrequencies,
					  size_t currentFrame,
					  std::vector<float>& phases,
					  float sampleRate,
					  int hopSize,
					  const std::vector<float>* prevOutputPhase);

}

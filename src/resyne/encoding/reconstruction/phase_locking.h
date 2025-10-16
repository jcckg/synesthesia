#pragma once

#include <vector>
#include <cstddef>

namespace PhaseReconstruction {

// Locks phases of bins near spectral peaks to peak phase for vertical phase coherence
// Enhances harmonic structure by enforcing phase consistency across overtones
void applyPhaseLocking(std::vector<float>& phases,
				       const std::vector<float>& magnitudes,
				       const std::vector<size_t>& peaks,
				       const std::vector<float>& damageWeights);

}

#pragma once

#include <vector>
#include <cstddef>

namespace PhaseReconstruction {

// Neighbouring bins share a peak's phase reference; magnitude and damage weights control the blend.
void applyPhaseLocking(std::vector<float>& phases,
				       const std::vector<float>& magnitudes,
				       const std::vector<size_t>& peaks,
				       const std::vector<float>& damageWeights);

}

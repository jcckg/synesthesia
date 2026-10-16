#pragma once

#include <vector>

namespace PhaseReconstruction {

// Sum only positive magnitude changes so falling bins cannot cancel an onset.
float computeSpectralFlux(const std::vector<float>& currentMag,
						  const std::vector<float>& previousMag);

}

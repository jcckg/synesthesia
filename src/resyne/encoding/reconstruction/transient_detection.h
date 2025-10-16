#pragma once

#include <vector>

namespace PhaseReconstruction {

// Computes spectral flux for transient detection
// Half-wave rectified sum of positive magnitude differences (Bello et al., 2005)
float computeSpectralFlux(const std::vector<float>& currentMag,
						  const std::vector<float>& previousMag);

}

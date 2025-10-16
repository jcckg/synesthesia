#pragma once

#include <vector>
#include <cstddef>

namespace PhaseReconstruction {

// Computes average magnitude gradient at a bin
float computeBinSharpness(const std::vector<float>& magnitudes, size_t bin);

// Detects bins damaged by visual editing operations
// Damskagg & Välimäki (2017) - fuzzy bin classification for time-scale modification
std::vector<bool> detectDamagedBins(const std::vector<std::vector<float>>& allMagnitudes,
								   size_t currentFrame);

// Finds local maxima in magnitude spectrum
std::vector<size_t> findSpectralPeaks(const std::vector<float>& magnitudes,
									   float minPeakMagnitude = 1e-4f);

// Computes smooth blend weights from binary damage mask
// Laroche & Dolson (1999) - raised-cosine windowing for phase locking
std::vector<float> computeDamageBlend(const std::vector<bool>& damagedBins, size_t radius);

}

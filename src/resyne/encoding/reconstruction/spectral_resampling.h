#pragma once

#include <cstddef>
#include <vector>

namespace SpectralResampling {

struct ResampledSpectrum {
	std::vector<float> magnitudes;
	std::vector<float> phases;
	bool frequencyShifted = false;
};

struct PhaseState {
	std::vector<float> previousPhases;
	std::vector<float> previousMagnitudes;
	std::vector<float> offsets;
};

ResampledSpectrum resampleSpectrum(
	const std::vector<float>& magnitudes,
	const std::vector<float>& phases,
	const std::vector<float>& decodedFrequencies,
	float sampleRate,
	size_t fftSize,
	int hopSize,
	PhaseState& phaseState
);

}

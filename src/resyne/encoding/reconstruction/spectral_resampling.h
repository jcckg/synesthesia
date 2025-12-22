#pragma once

#include <cstddef>
#include <vector>

namespace SpectralResampling {

struct ResampledSpectrum {
	std::vector<float> magnitudes;
	std::vector<float> phases;
};

ResampledSpectrum resampleSpectrum(
	const std::vector<float>& magnitudes,
	const std::vector<float>& phases,
	const std::vector<float>& decodedFrequencies,
	float sampleRate,
	size_t fftSize
);

float computeShiftRatio(float decodedFrequency, float expectedFrequency);

}

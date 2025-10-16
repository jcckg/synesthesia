#pragma once

#include <span>
#include <vector>
#include "colour_mapper.h"
#include "fft_processor.h"
#include "constants.h"

#ifdef USE_NEON_OPTIMISATIONS
#include "neon/spectral_processor_neon.h"
#endif
#ifdef USE_SSE_OPTIMISATIONS
#include "sse/spectral_processor_sse.h"
#endif

class SpectralProcessor {
public:
	struct SpectralColourResult {
		float r, g, b;
		float X, Y, Z;
		float L, a, b_comp;
		float dominantWavelength;
		float dominantFrequency;
		float spectralCentroid;
		float spectralFlatness;
		float spectralSpread;
		float spectralRolloff;
		float spectralCrestFactor;
	};

	static SpectralColourResult spectrumToColour(
		std::span<const float> magnitudes,
		std::span<const float> phases,
		float sampleRate,
		float gamma = 1.0f,
		ColourMapper::ColourSpace colourSpace = ColourMapper::ColourSpace::Rec2020,
		bool applyGamutMapping = true
	);

	static SpectralColourResult spectrumToColour(
		const std::vector<FFTProcessor::ComplexBin>& spectrum,
		float gamma = 1.0f,
		ColourMapper::ColourSpace colourSpace = ColourMapper::ColourSpace::Rec2020,
		bool applyGamutMapping = true
	);

private:
	static constexpr float MIN_FREQ = synesthesia::constants::MIN_AUDIO_FREQ;
	static constexpr float MAX_FREQ = synesthesia::constants::MAX_AUDIO_FREQ;
	static constexpr size_t SPECTRUM_SIZE = 1025;

	static float calculateSpectralCentroid(
		std::span<const float> magnitudes,
		std::span<const float> frequencies
	);

	static float calculateSpectralSpread(
		std::span<const float> magnitudes,
		std::span<const float> frequencies,
		float centroid
	);

	static float calculateSpectralFlatness(
		std::span<const float> magnitudes
	);

	static float calculateSpectralRolloff(
		std::span<const float> magnitudes,
		std::span<const float> frequencies,
		float threshold = 0.85f
	);

	static float calculateSpectralCrestFactor(
		std::span<const float> magnitudes,
		float maxMagnitude,
		float totalEnergy
	);

	static void integrateSpectrumCIE(
		std::span<const float> magnitudes,
		std::span<const float> frequencies,
		float& X_total,
		float& Y_total,
		float& Z_total
	);

	static void applyPerceptualWeighting(
		std::vector<float>& magnitudes,
		std::span<const float> frequencies
	);
};

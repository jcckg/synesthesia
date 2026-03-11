#pragma once

#if defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <immintrin.h>
#include <span>

namespace SpectralProcessorSSE {
	void applyPerceptualWeighting(
		std::span<float> magnitudes,
		std::span<const float> frequencies,
		size_t count
	);

	void calculateSpectralCentroid(
		std::span<const float> magnitudes,
		std::span<const float> frequencies,
		size_t count,
		float& weightedSum,
		float& totalWeight
	);

	void calculateSpectralSpread(
		std::span<const float> magnitudes,
		std::span<const float> frequencies,
		size_t count,
		float centroid,
		float& spreadSum,
		float& totalWeight
	);

	void calculateSpectralFlatness(
		std::span<const float> magnitudes,
		size_t count,
		float& geometricSum,
		float& arithmeticSum,
		int& validCount
	);

	void integrateSpectrumCIE(
		std::span<const float> magnitudes,
		std::span<const float> X_values,
		std::span<const float> Y_values,
		std::span<const float> Z_values,
		size_t count,
		float& X_total,
		float& Y_total,
		float& Z_total
	);

	bool isSSEAvailable();
}

#endif

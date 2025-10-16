#include "spectral_processor_sse.h"

#if defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)

#include <algorithm>
#include <cmath>

namespace SpectralProcessorSSE {

bool isSSEAvailable() {
	return true;
}

namespace {
	inline void horizontalAdd(const __m128 value, float& out) {
		alignas(16) float tmp[4];
		_mm_store_ps(tmp, value);
		out = tmp[0] + tmp[1] + tmp[2] + tmp[3];
	}

	inline __m128 elementwiseSqrt(const __m128 input) {
		alignas(16) float tmp[4];
		_mm_store_ps(tmp, input);
		for (float& v : tmp) {
			v = std::sqrt(v);
		}
		return _mm_load_ps(tmp);
	}

	inline __m128 elementwiseLog(const __m128 input) {
		alignas(16) float tmp[4];
		_mm_store_ps(tmp, input);
		for (float& v : tmp) {
			v = std::log(v);
		}
		return _mm_load_ps(tmp);
	}

	inline __m128 elementwiseLog10(const __m128 input) {
		alignas(16) float tmp[4];
		_mm_store_ps(tmp, input);
		for (float& v : tmp) {
			v = std::log10(v);
		}
		return _mm_load_ps(tmp);
	}

	inline __m128 elementwiseExp(const __m128 input) {
		alignas(16) float tmp[4];
		_mm_store_ps(tmp, input);
		for (float& v : tmp) {
			v = std::exp(v);
		}
		return _mm_load_ps(tmp);
	}
}

void applyPerceptualWeighting(
	std::span<float> magnitudes,
	std::span<const float> frequencies,
	const size_t count
) {
	const size_t simdCount = count & ~size_t{3};

	const __m128 v12194sq = _mm_set1_ps(12194.0f * 12194.0f);
	const __m128 v20sq = _mm_set1_ps(20.6f * 20.6f);
	const __m128 v107sq = _mm_set1_ps(107.7f * 107.7f);
	const __m128 v737sq = _mm_set1_ps(737.9f * 737.9f);
	const __m128 vTwo = _mm_set1_ps(20.0f);
	const __m128 vDbFactor = _mm_set1_ps(0.11512925f);
	const __m128 vDbOffset = _mm_set1_ps(2.0f);
	const __m128 vZero = _mm_set1_ps(0.0f);
	const __m128 vMaxGain = _mm_set1_ps(4.0f);

	for (size_t i = 0; i < simdCount; i += 4) {
		__m128 freq = _mm_loadu_ps(&frequencies[i]);
		__m128 mag = _mm_loadu_ps(&magnitudes[i]);

		__m128 f2 = _mm_mul_ps(freq, freq);
		__m128 f4 = _mm_mul_ps(f2, f2);

		// IEC 61672-1:2013 A-weighting numerator = (12194^2) * f^4
		__m128 numerator = _mm_mul_ps(v12194sq, f4);

		__m128 term1 = _mm_add_ps(f2, v20sq);
		__m128 term2 = _mm_add_ps(f2, v107sq);
		__m128 term3 = _mm_add_ps(f2, v737sq);
		__m128 term4 = _mm_add_ps(f2, v12194sq);

		__m128 sqrtTerm = elementwiseSqrt(_mm_mul_ps(term2, term3));
		__m128 denominator = _mm_mul_ps(_mm_mul_ps(term1, sqrtTerm), term4);

		__m128 aWeight = _mm_div_ps(numerator, denominator);
		__m128 dbAdjustment = _mm_add_ps(_mm_mul_ps(vTwo, elementwiseLog10(aWeight)), vDbOffset);
		__m128 scaled = _mm_mul_ps(dbAdjustment, vDbFactor);
		__m128 gain = elementwiseExp(scaled);

		gain = _mm_min_ps(_mm_max_ps(gain, vZero), vMaxGain);
		__m128 weighted = _mm_mul_ps(mag, gain);
		_mm_storeu_ps(&magnitudes[i], weighted);
	}

	for (size_t i = simdCount; i < count; ++i) {
		const float freq = frequencies[i];
		const float f2 = freq * freq;
		const float numerator = 12194.0f * 12194.0f * f2 * f2;
		const float denominator = (f2 + 20.6f * 20.6f) *
			std::sqrt((f2 + 107.7f * 107.7f) * (f2 + 737.9f * 737.9f)) *
			(f2 + 12194.0f * 12194.0f);
		const float aWeight = numerator / denominator;
		const float dbAdjustment = 20.0f * std::log10(aWeight) + 2.0f;
		const float perceptualGain = std::exp(dbAdjustment * 0.11512925f);
		magnitudes[i] *= std::clamp(perceptualGain, 0.0f, 4.0f);
	}
}

void calculateSpectralCentroid(
	std::span<const float> magnitudes,
	std::span<const float> frequencies,
	const size_t count,
	float& weightedSum,
	float& totalWeight
) {
	const size_t simdCount = count & ~size_t{3};

	__m128 weightedVec = _mm_setzero_ps();
	__m128 weightVec = _mm_setzero_ps();

	for (size_t i = 0; i < simdCount; i += 4) {
		__m128 mag = _mm_loadu_ps(&magnitudes[i]);
		__m128 freq = _mm_loadu_ps(&frequencies[i]);

		weightedVec = _mm_add_ps(weightedVec, _mm_mul_ps(freq, mag));
		weightVec = _mm_add_ps(weightVec, mag);
	}

	horizontalAdd(weightedVec, weightedSum);
	horizontalAdd(weightVec, totalWeight);

	for (size_t i = simdCount; i < count; ++i) {
		const float mag = magnitudes[i];
		const float freq = frequencies[i];
		weightedSum += freq * mag;
		totalWeight += mag;
	}
}

void calculateSpectralSpread(
	std::span<const float> magnitudes,
	std::span<const float> frequencies,
	const size_t count,
	const float centroid,
	float& spreadSum,
	float& totalWeight
) {
	const size_t simdCount = count & ~size_t{3};
	const __m128 centroidVec = _mm_set1_ps(centroid);

	__m128 spreadVec = _mm_setzero_ps();
	__m128 weightVec = _mm_setzero_ps();

	for (size_t i = 0; i < simdCount; i += 4) {
		__m128 mag = _mm_loadu_ps(&magnitudes[i]);
		__m128 freq = _mm_loadu_ps(&frequencies[i]);

		__m128 diff = _mm_sub_ps(freq, centroidVec);
		__m128 diffSq = _mm_mul_ps(diff, diff);
		__m128 weighted = _mm_mul_ps(mag, diffSq);

		spreadVec = _mm_add_ps(spreadVec, weighted);
		weightVec = _mm_add_ps(weightVec, mag);
	}

	horizontalAdd(spreadVec, spreadSum);
	horizontalAdd(weightVec, totalWeight);

	for (size_t i = simdCount; i < count; ++i) {
		const float mag = magnitudes[i];
		const float freq = frequencies[i];
		const float diff = freq - centroid;
		spreadSum += mag * diff * diff;
		totalWeight += mag;
	}
}

void calculateSpectralFlatness(
	std::span<const float> magnitudes,
	const size_t count,
	float& geometricSum,
	float& arithmeticSum,
	int& validCount
) {
	const size_t simdCount = count & ~size_t{3};
	const __m128 threshold = _mm_set1_ps(1e-6f);
	const __m128 ones = _mm_set1_ps(1.0f);

	__m128 geoVec = _mm_setzero_ps();
	__m128 arithVec = _mm_setzero_ps();
	__m128 countVec = _mm_setzero_ps();

	for (size_t i = 0; i < simdCount; i += 4) {
		__m128 mag = _mm_loadu_ps(&magnitudes[i]);
		__m128 mask = _mm_cmpgt_ps(mag, threshold);

		__m128 maskedMag = _mm_and_ps(mask, mag);
		arithVec = _mm_add_ps(arithVec, maskedMag);

		__m128 laneCount = _mm_and_ps(mask, ones);
		countVec = _mm_add_ps(countVec, laneCount);

		__m128 logInput = _mm_or_ps(_mm_and_ps(mask, mag), ones);
		geoVec = _mm_add_ps(geoVec, elementwiseLog(logInput));
	}

	float geoAcc = 0.0f;
	float arithAcc = 0.0f;
	float countAcc = 0.0f;
	horizontalAdd(geoVec, geoAcc);
	horizontalAdd(arithVec, arithAcc);
	horizontalAdd(countVec, countAcc);

	geometricSum += geoAcc;
	arithmeticSum += arithAcc;
	validCount += static_cast<int>(countAcc + 0.5f);

	for (size_t i = simdCount; i < count; ++i) {
		const float mag = magnitudes[i];
		if (mag > 1e-6f) {
			geometricSum += std::log(mag);
			arithmeticSum += mag;
			validCount++;
		}
	}
}

}

#endif

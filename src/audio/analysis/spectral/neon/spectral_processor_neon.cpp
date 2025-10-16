#include "spectral_processor_neon.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#include <cmath>

namespace SpectralProcessorNEON {

bool isNEONAvailable() {
	return true;
}

// A-weighting curve (IEC 61672-1:2013)
// Pole frequencies: 20.6 Hz, 107.7 Hz, 737.9 Hz, 12194 Hz
void applyPerceptualWeighting(
	std::span<float> magnitudes,
	std::span<const float> frequencies,
	const size_t count
) {
	const size_t simd_count = count & ~size_t{3};

	const float32x4_t v_12194_sq = vdupq_n_f32(12194.0f * 12194.0f);
	const float32x4_t v_20_6_sq = vdupq_n_f32(20.6f * 20.6f);
	const float32x4_t v_107_7_sq = vdupq_n_f32(107.7f * 107.7f);
	const float32x4_t v_737_9_sq = vdupq_n_f32(737.9f * 737.9f);
	const float32x4_t v_twenty = vdupq_n_f32(20.0f);
	const float32x4_t v_db_factor = vdupq_n_f32(0.11512925f);
	const float32x4_t v_db_offset = vdupq_n_f32(2.0f);
	const float32x4_t v_min_gain = vdupq_n_f32(0.0f);
	const float32x4_t v_max_gain = vdupq_n_f32(4.0f);

	for (size_t i = 0; i < simd_count; i += 4) {
		float32x4_t freq = vld1q_f32(&frequencies[i]);
		float32x4_t mag = vld1q_f32(&magnitudes[i]);

		float32x4_t f2 = vmulq_f32(freq, freq);
		float32x4_t f4 = vmulq_f32(f2, f2);

		float32x4_t numerator = vmulq_f32(v_12194_sq, v_12194_sq);
		numerator = vmulq_f32(numerator, f4);

		float32x4_t term1 = vaddq_f32(f2, v_20_6_sq);
		float32x4_t term2 = vaddq_f32(f2, v_107_7_sq);
		float32x4_t term3 = vaddq_f32(f2, v_737_9_sq);
		float32x4_t term4 = vaddq_f32(f2, v_12194_sq);

		float32x4_t sqrt_term = vmulq_f32(term2, term3);
		float sqrt_values[4];
		vst1q_f32(sqrt_values, sqrt_term);
		for (int j = 0; j < 4; ++j) {
			sqrt_values[j] = std::sqrt(sqrt_values[j]);
		}
		sqrt_term = vld1q_f32(sqrt_values);

		float32x4_t denominator = vmulq_f32(term1, sqrt_term);
		denominator = vmulq_f32(denominator, term4);

		float32x4_t aWeight = vdivq_f32(numerator, denominator);

		float log_values[4];
		vst1q_f32(log_values, aWeight);
		for (int j = 0; j < 4; ++j) {
			log_values[j] = std::log10(log_values[j]);
		}
		float32x4_t log_aWeight = vld1q_f32(log_values);

		float32x4_t dbAdjustment = vmlaq_f32(v_db_offset, v_twenty, log_aWeight);

		float32x4_t scaled = vmulq_f32(dbAdjustment, v_db_factor);

		float exp_values[4];
		vst1q_f32(exp_values, scaled);
		for (int j = 0; j < 4; ++j) {
			exp_values[j] = std::exp(exp_values[j]);
		}
		float32x4_t perceptualGain = vld1q_f32(exp_values);

		perceptualGain = vmaxq_f32(perceptualGain, v_min_gain);
		perceptualGain = vminq_f32(perceptualGain, v_max_gain);

		float32x4_t weighted = vmulq_f32(mag, perceptualGain);
		vst1q_f32(&magnitudes[i], weighted);
	}

	for (size_t i = simd_count; i < count; ++i) {
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
	const size_t simd_count = count & ~size_t{3};

	float32x4_t v_weighted_sum = vdupq_n_f32(0.0f);
	float32x4_t v_total_weight = vdupq_n_f32(0.0f);

	for (size_t i = 0; i < simd_count; i += 4) {
		float32x4_t mag = vld1q_f32(&magnitudes[i]);
		float32x4_t freq = vld1q_f32(&frequencies[i]);

		float32x4_t weighted = vmulq_f32(freq, mag);
		v_weighted_sum = vaddq_f32(v_weighted_sum, weighted);
		v_total_weight = vaddq_f32(v_total_weight, mag);
	}

	float weighted_array[4], weight_array[4];
	vst1q_f32(weighted_array, v_weighted_sum);
	vst1q_f32(weight_array, v_total_weight);

	weightedSum = weighted_array[0] + weighted_array[1] + weighted_array[2] + weighted_array[3];
	totalWeight = weight_array[0] + weight_array[1] + weight_array[2] + weight_array[3];

	for (size_t i = simd_count; i < count; ++i) {
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
	const size_t simd_count = count & ~size_t{3};

	float32x4_t v_centroid = vdupq_n_f32(centroid);
	float32x4_t v_spread_sum = vdupq_n_f32(0.0f);
	float32x4_t v_total_weight = vdupq_n_f32(0.0f);

	for (size_t i = 0; i < simd_count; i += 4) {
		float32x4_t mag = vld1q_f32(&magnitudes[i]);
		float32x4_t freq = vld1q_f32(&frequencies[i]);

		float32x4_t diff = vsubq_f32(freq, v_centroid);
		float32x4_t diff_sq = vmulq_f32(diff, diff);
		float32x4_t weighted = vmulq_f32(mag, diff_sq);

		v_spread_sum = vaddq_f32(v_spread_sum, weighted);
		v_total_weight = vaddq_f32(v_total_weight, mag);
	}

	float spread_array[4], weight_array[4];
	vst1q_f32(spread_array, v_spread_sum);
	vst1q_f32(weight_array, v_total_weight);

	spreadSum = spread_array[0] + spread_array[1] + spread_array[2] + spread_array[3];
	totalWeight = weight_array[0] + weight_array[1] + weight_array[2] + weight_array[3];

	for (size_t i = simd_count; i < count; ++i) {
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
	const size_t simd_count = count & ~size_t{3};

	float32x4_t v_geometric_sum = vdupq_n_f32(0.0f);
	float32x4_t v_arithmetic_sum = vdupq_n_f32(0.0f);
	int32x4_t v_valid_count = vdupq_n_s32(0);
	const float32x4_t v_threshold = vdupq_n_f32(1e-6f);
	const int32x4_t v_one = vdupq_n_s32(1);

	for (size_t i = 0; i < simd_count; i += 4) {
		float32x4_t mag = vld1q_f32(&magnitudes[i]);
		uint32x4_t valid = vcgtq_f32(mag, v_threshold);

		float log_values[4];
		vst1q_f32(log_values, mag);
		for (int j = 0; j < 4; ++j) {
			log_values[j] = std::log(log_values[j]);
		}
		float32x4_t log_mag = vld1q_f32(log_values);

		float32x4_t masked_log = vbslq_f32(valid, log_mag, vdupq_n_f32(0.0f));
		float32x4_t masked_mag = vbslq_f32(valid, mag, vdupq_n_f32(0.0f));
		int32x4_t masked_count = vbslq_s32(valid, v_one, vdupq_n_s32(0));

		v_geometric_sum = vaddq_f32(v_geometric_sum, masked_log);
		v_arithmetic_sum = vaddq_f32(v_arithmetic_sum, masked_mag);
		v_valid_count = vaddq_s32(v_valid_count, masked_count);
	}

	float geom_array[4], arith_array[4];
	int32_t count_array[4];
	vst1q_f32(geom_array, v_geometric_sum);
	vst1q_f32(arith_array, v_arithmetic_sum);
	vst1q_s32(count_array, v_valid_count);

	geometricSum = geom_array[0] + geom_array[1] + geom_array[2] + geom_array[3];
	arithmeticSum = arith_array[0] + arith_array[1] + arith_array[2] + arith_array[3];
	validCount = count_array[0] + count_array[1] + count_array[2] + count_array[3];

	for (size_t i = simd_count; i < count; ++i) {
		const float mag = magnitudes[i];
		if (mag > 1e-6f) {
			geometricSum += std::log(mag);
			arithmeticSum += mag;
			validCount++;
		}
	}
}

void integrateSpectrumCIE(
	std::span<const float> magnitudes,
	std::span<const float> X_values,
	std::span<const float> Y_values,
	std::span<const float> Z_values,
	const size_t count,
	float& X_total,
	float& Y_total,
	float& Z_total
) {
	const size_t simd_count = count & ~size_t{3};

	float32x4_t v_X_total = vdupq_n_f32(0.0f);
	float32x4_t v_Y_total = vdupq_n_f32(0.0f);
	float32x4_t v_Z_total = vdupq_n_f32(0.0f);

	for (size_t i = 0; i < simd_count; i += 4) {
		float32x4_t mag = vld1q_f32(&magnitudes[i]);
		float32x4_t X = vld1q_f32(&X_values[i]);
		float32x4_t Y = vld1q_f32(&Y_values[i]);
		float32x4_t Z = vld1q_f32(&Z_values[i]);

		v_X_total = vmlaq_f32(v_X_total, mag, X);
		v_Y_total = vmlaq_f32(v_Y_total, mag, Y);
		v_Z_total = vmlaq_f32(v_Z_total, mag, Z);
	}

	float X_array[4], Y_array[4], Z_array[4];
	vst1q_f32(X_array, v_X_total);
	vst1q_f32(Y_array, v_Y_total);
	vst1q_f32(Z_array, v_Z_total);

	X_total = X_array[0] + X_array[1] + X_array[2] + X_array[3];
	Y_total = Y_array[0] + Y_array[1] + Y_array[2] + Y_array[3];
	Z_total = Z_array[0] + Z_array[1] + Z_array[2] + Z_array[3];

	for (size_t i = simd_count; i < count; ++i) {
		const float mag = magnitudes[i];
		X_total += mag * X_values[i];
		Y_total += mag * Y_values[i];
		Z_total += mag * Z_values[i];
	}
}

}

#endif
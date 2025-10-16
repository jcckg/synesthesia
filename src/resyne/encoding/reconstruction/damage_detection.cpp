#include "damage_detection.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace PhaseReconstruction {

namespace {
constexpr float EPSILON = 1e-6f;
constexpr float MIN_BIN_INTENSITY = 1e-6f;
}

float computeBinSharpness(const std::vector<float>& magnitudes, size_t bin) {
	if (magnitudes.empty() || bin >= magnitudes.size()) {
		return 0.0f;
	}

	const float centre = magnitudes[bin];
	float gradientSum = 0.0f;
	float samples = 0.0f;

	if (bin > 0) {
		gradientSum += std::abs(centre - magnitudes[bin - 1]);
		samples += 1.0f;
	}

	if (bin + 1 < magnitudes.size()) {
		gradientSum += std::abs(centre - magnitudes[bin + 1]);
		samples += 1.0f;
	}

	return samples > 0.0f ? gradientSum / samples : 0.0f;
}

std::vector<bool> detectDamagedBins(const std::vector<std::vector<float>>& allMagnitudes,
								   size_t currentFrame) {
	if (allMagnitudes.empty() || currentFrame >= allMagnitudes.size()) {
		return std::vector<bool>();
	}

	const auto& currentMagnitudes = allMagnitudes[currentFrame];
	const size_t binCount = currentMagnitudes.size();
	std::vector<bool> isDamaged(binCount, false);

	constexpr size_t TEMPORAL_RADIUS = 3;
	constexpr float SHARPNESS_RATIO = 0.85f;
	constexpr float MAG_STABILITY_RATIO = 0.1f;
	constexpr float MAG_DROP_RATIO = 0.6f;
	constexpr float CONTEXT_RATIO = 0.5f;
	const size_t frameStart = currentFrame > TEMPORAL_RADIUS ? currentFrame - TEMPORAL_RADIUS : 0;
	const size_t frameEnd = std::min(currentFrame + TEMPORAL_RADIUS + 1, allMagnitudes.size());

	for (size_t bin = 0; bin < binCount; ++bin) {
		const float currentMag = currentMagnitudes[bin];
		if (currentMag <= MIN_BIN_INTENSITY) {
			continue;
		}

		float temporalSharpness = 0.0f;
		float temporalMagnitude = 0.0f;
		size_t temporalCount = 0;

		for (size_t frame = frameStart; frame < frameEnd; ++frame) {
			if (frame == currentFrame) {
				continue;
			}
			if (bin >= allMagnitudes[frame].size()) {
				continue;
			}

			const float neighbourMag = allMagnitudes[frame][bin];
			if (neighbourMag <= MIN_BIN_INTENSITY) {
				continue;
			}

			temporalSharpness += computeBinSharpness(allMagnitudes[frame], bin);
			temporalMagnitude += neighbourMag;
			temporalCount++;
		}

		if (temporalCount < 2) {
			continue;
		}

		temporalSharpness /= static_cast<float>(temporalCount);
		temporalMagnitude /= static_cast<float>(temporalCount);

		const float currentSharpness = computeBinSharpness(currentMagnitudes, bin);
		float contextSharpness = 0.0f;
		size_t contextCount = 0;

		for (int offset = -2; offset <= 2; ++offset) {
			if (offset == 0) {
				continue;
			}

			const int neighbourIndex = static_cast<int>(bin) + offset;
			if (neighbourIndex < 0 || neighbourIndex >= static_cast<int>(binCount)) {
				continue;
			}

			contextSharpness += computeBinSharpness(currentMagnitudes, static_cast<size_t>(neighbourIndex));
			contextCount++;
		}

		const float avgContextSharpness = contextCount > 0
			? contextSharpness / static_cast<float>(contextCount)
			: currentSharpness;

		const bool sharpnessDrop = (temporalSharpness > EPSILON) &&
			(currentSharpness < temporalSharpness * SHARPNESS_RATIO);
		const bool magnitudeStable = (temporalMagnitude > EPSILON) &&
			(std::abs(currentMag - temporalMagnitude) < temporalMagnitude * MAG_STABILITY_RATIO);
		const bool magnitudeDrop = (temporalMagnitude > EPSILON) &&
			(currentMag < temporalMagnitude * MAG_DROP_RATIO);
		const bool localContrast = avgContextSharpness > currentSharpness * CONTEXT_RATIO;

		isDamaged[bin] = sharpnessDrop && magnitudeStable && magnitudeDrop && localContrast;
	}

	return isDamaged;
}

std::vector<size_t> findSpectralPeaks(const std::vector<float>& magnitudes,
									   float minPeakMagnitude) {
	std::vector<size_t> peaks;

	if (magnitudes.size() < 3) {
		return peaks;
	}

	for (size_t i = 1; i < magnitudes.size() - 1; ++i) {
		if (magnitudes[i] > minPeakMagnitude &&
			magnitudes[i] > magnitudes[i-1] &&
			magnitudes[i] > magnitudes[i+1]) {
			peaks.push_back(i);
		}
	}

	return peaks;
}

std::vector<float> computeDamageBlend(const std::vector<bool>& damagedBins, size_t radius) {
	std::vector<float> weights(damagedBins.size(), 0.0f);
	if (damagedBins.empty()) {
		return weights;
	}

	if (radius == 0) {
		for (size_t i = 0; i < damagedBins.size(); ++i) {
			weights[i] = damagedBins[i] ? 1.0f : 0.0f;
		}
		return weights;
	}

	const float denom = static_cast<float>(radius) + 1.0f;

	for (size_t bin = 0; bin < damagedBins.size(); ++bin) {
		float weightedSum = 0.0f;
		float weightTotal = 0.0f;

		for (int offset = -static_cast<int>(radius); offset <= static_cast<int>(radius); ++offset) {
			const int neighbourIndex = static_cast<int>(bin) + offset;
			if (neighbourIndex < 0 || neighbourIndex >= static_cast<int>(damagedBins.size())) {
				continue;
			}

			const float windowPhase = static_cast<float>(offset) * std::numbers::pi_v<float> / denom;
			const float windowWeight = 0.5f * (1.0f + std::cos(windowPhase));

			weightTotal += windowWeight;
			if (damagedBins[static_cast<size_t>(neighbourIndex)]) {
				weightedSum += windowWeight;
			}
		}

		if (weightTotal > 0.0f) {
			weights[bin] = std::clamp(weightedSum / weightTotal, 0.0f, 1.0f);
		}

		if (damagedBins[bin]) {
			weights[bin] = 1.0f;
		} else {
			weights[bin] = std::min(weights[bin], 0.35f);
		}
	}

	return weights;
}

}

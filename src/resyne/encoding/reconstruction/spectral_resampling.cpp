#include "spectral_resampling.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace SpectralResampling {

namespace {
constexpr float EPSILON = 1e-6f;
constexpr float MIN_SHIFT_RATIO = 0.25f;
constexpr float MAX_SHIFT_RATIO = 4.0f;
constexpr float SHIFT_DETECTION_THRESHOLD = 0.02f;
}

float computeShiftRatio(float decodedFrequency, float expectedFrequency) {
	if (expectedFrequency < EPSILON || decodedFrequency < EPSILON) {
		return 1.0f;
	}
	return decodedFrequency / expectedFrequency;
}

ResampledSpectrum resampleSpectrum(
	const std::vector<float>& magnitudes,
	const std::vector<float>& phases,
	const std::vector<float>& decodedFrequencies,
	float sampleRate,
	size_t fftSize
) {
	ResampledSpectrum result;
	const size_t numBins = magnitudes.size();

	if (numBins == 0 || phases.size() != numBins || decodedFrequencies.size() != numBins) {
		result.magnitudes = magnitudes;
		result.phases = phases;
		return result;
	}

	result.magnitudes.assign(numBins, 0.0f);
	result.phases.assign(numBins, 0.0f);

	const float freqResolution = (fftSize > 0 && sampleRate > 0.0f)
		? sampleRate / static_cast<float>(fftSize)
		: 1.0f;

	std::vector<float> shiftRatios(numBins, 1.0f);
	bool hasSignificantShift = false;

	for (size_t bin = 1; bin < numBins; ++bin) {
		const float expectedFreq = freqResolution * static_cast<float>(bin);
		const float decodedFreq = decodedFrequencies[bin];

		if (expectedFreq > EPSILON && decodedFreq > EPSILON) {
			const float ratio = decodedFreq / expectedFreq;
			shiftRatios[bin] = std::clamp(ratio, MIN_SHIFT_RATIO, MAX_SHIFT_RATIO);

			if (std::abs(ratio - 1.0f) > SHIFT_DETECTION_THRESHOLD) {
				hasSignificantShift = true;
			}
		}
	}

	if (!hasSignificantShift) {
		result.magnitudes = magnitudes;
		result.phases = phases;
		return result;
	}

	std::vector<float> accumulatedMag(numBins, 0.0f);
	std::vector<float> accumulatedPhaseX(numBins, 0.0f);
	std::vector<float> accumulatedPhaseY(numBins, 0.0f);
	std::vector<float> accumulatedWeight(numBins, 0.0f);

	for (size_t srcBin = 1; srcBin < numBins; ++srcBin) {
		const float mag = magnitudes[srcBin];
		if (mag < EPSILON) {
			continue;
		}

		const float ratio = shiftRatios[srcBin];
		const float targetBinF = static_cast<float>(srcBin) * ratio;

		if (targetBinF < 0.5f || targetBinF >= static_cast<float>(numBins) - 0.5f) {
			continue;
		}

		const size_t targetBinLow = static_cast<size_t>(std::floor(targetBinF));
		const size_t targetBinHigh = targetBinLow + 1;
		const float fracHigh = targetBinF - static_cast<float>(targetBinLow);
		const float fracLow = 1.0f - fracHigh;

		const float phase = phases[srcBin];
		const float phaseX = std::cos(phase);
		const float phaseY = std::sin(phase);

		if (targetBinLow > 0 && targetBinLow < numBins) {
			accumulatedMag[targetBinLow] += mag * fracLow;
			accumulatedPhaseX[targetBinLow] += phaseX * mag * fracLow;
			accumulatedPhaseY[targetBinLow] += phaseY * mag * fracLow;
			accumulatedWeight[targetBinLow] += fracLow;
		}

		if (targetBinHigh > 0 && targetBinHigh < numBins) {
			accumulatedMag[targetBinHigh] += mag * fracHigh;
			accumulatedPhaseX[targetBinHigh] += phaseX * mag * fracHigh;
			accumulatedPhaseY[targetBinHigh] += phaseY * mag * fracHigh;
			accumulatedWeight[targetBinHigh] += fracHigh;
		}
	}

	result.magnitudes[0] = magnitudes[0];
	result.phases[0] = phases[0];

	for (size_t bin = 1; bin < numBins; ++bin) {
		if (accumulatedWeight[bin] > EPSILON) {
			result.magnitudes[bin] = accumulatedMag[bin];

			const float avgPhaseX = accumulatedPhaseX[bin];
			const float avgPhaseY = accumulatedPhaseY[bin];
			result.phases[bin] = std::atan2(avgPhaseY, avgPhaseX);
		}
	}

	return result;
}

}

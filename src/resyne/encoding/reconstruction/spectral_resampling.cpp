#include "spectral_resampling.h"
#include "constants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

namespace SpectralResampling {

namespace {
constexpr float EPSILON = 1e-6f;
constexpr float MIN_SHIFT_RATIO = 0.25f;
constexpr float MAX_SHIFT_RATIO = 4.0f;
constexpr float SHIFT_DETECTION_THRESHOLD = 0.02f;
constexpr int INTERPOLATION_RADIUS = 8;

float sinc(float distance) {
	if (std::abs(distance) < EPSILON) {
		return 1.0f;
	}
	const float angle = std::numbers::pi_v<float> * distance;
	return std::sin(angle) / angle;
}

std::array<float, INTERPOLATION_RADIUS * 2> interpolationKernel(float fraction) {
	std::array<float, INTERPOLATION_RADIUS * 2> weights{};
	float sum = 0.0f;
	for (size_t tap = 0; tap < weights.size(); ++tap) {
		const float distance = fraction - static_cast<float>(static_cast<int>(tap) - INTERPOLATION_RADIUS + 1);
		if (std::abs(distance) < static_cast<float>(INTERPOLATION_RADIUS)) {
			weights[tap] = sinc(distance) * sinc(distance / static_cast<float>(INTERPOLATION_RADIUS));
			sum += weights[tap];
		}
	}
	for (float& weight : weights) {
		weight /= sum;
	}
	return weights;
}

bool isUsableFrequencyMapping(float expectedFrequency, float decodedFrequency, float magnitude) {
	if (magnitude <= EPSILON) {
		return false;
	}
	if (!std::isfinite(expectedFrequency) || !std::isfinite(decodedFrequency)) {
		return false;
	}
	if (expectedFrequency < synesthesia::constants::MIN_AUDIO_FREQ ||
		expectedFrequency > synesthesia::constants::MAX_AUDIO_FREQ) {
		return false;
	}
	if (decodedFrequency <= 0.0f) {
		return false;
	}
	return true;
}
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
	size_t fftSize,
	int hopSize,
	PhaseState& phaseState
) {
	ResampledSpectrum result;
	const size_t numBins = magnitudes.size();

	if (numBins == 0 || phases.size() != numBins || decodedFrequencies.size() != numBins ||
		fftSize == 0 || sampleRate <= 0.0f || hopSize <= 0) {
		result.magnitudes = magnitudes;
		result.phases = phases;
		phaseState = {};
		return result;
	}

	result.magnitudes.assign(numBins, 0.0f);
	result.phases.assign(numBins, 0.0f);

	const float freqResolution = sampleRate / static_cast<float>(fftSize);
	const float maximumFrequency = std::min(sampleRate * 0.5f, synesthesia::constants::MAX_AUDIO_FREQ);

	std::vector<float> shiftRatios(numBins, 1.0f);
	bool hasSignificantShift = false;

	for (size_t bin = 1; bin < numBins; ++bin) {
		const float expectedFreq = freqResolution * static_cast<float>(bin);
		const float decodedFreq = decodedFrequencies[bin];

		if (!isUsableFrequencyMapping(expectedFreq, decodedFreq, magnitudes[bin])) {
			continue;
		}

		const float ratio = decodedFreq / expectedFreq;
		shiftRatios[bin] = std::clamp(ratio, MIN_SHIFT_RATIO, MAX_SHIFT_RATIO);

		if (std::abs(ratio - 1.0f) > SHIFT_DETECTION_THRESHOLD || decodedFreq > maximumFrequency) {
			hasSignificantShift = true;
		}
	}

	if (!hasSignificantShift) {
		result.magnitudes = magnitudes;
		result.phases = phases;
		phaseState.previousPhases = phases;
		phaseState.previousMagnitudes = magnitudes;
		phaseState.offsets.assign(numBins, 0.0f);
		return result;
	}
	result.frequencyShifted = true;

	constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
	std::vector<float> instantaneousBins(numBins);
	for (size_t bin = 0; bin < numBins; ++bin) {
		instantaneousBins[bin] = static_cast<float>(bin);
	}
	if (phaseState.previousPhases.size() != numBins ||
		phaseState.previousMagnitudes.size() != numBins || phaseState.offsets.size() != numBins) {
		phaseState.offsets.assign(numBins, 0.0f);
	} else {
		for (size_t bin = 0; bin < numBins; ++bin) {
			if (magnitudes[bin] <= EPSILON || phaseState.previousMagnitudes[bin] <= EPSILON ||
				std::abs(shiftRatios[bin] - 1.0f) <= SHIFT_DETECTION_THRESHOLD) {
				phaseState.offsets[bin] = 0.0f;
				continue;
			}
			const float expectedAdvance = twoPi * static_cast<float>(bin) *
				static_cast<float>(hopSize) / static_cast<float>(fftSize);
			const float advance = expectedAdvance + std::remainder(
				phases[bin] - phaseState.previousPhases[bin] - expectedAdvance, twoPi);
			instantaneousBins[bin] = advance * static_cast<float>(fftSize) /
				(twoPi * static_cast<float>(hopSize));
			phaseState.offsets[bin] = std::remainder(
				phaseState.offsets[bin] + advance * (shiftRatios[bin] - 1.0f), twoPi);
		}
	}
	phaseState.previousPhases = phases;
	phaseState.previousMagnitudes = magnitudes;
	std::vector<size_t> peaks;
	for (size_t bin = 1; bin + 1 < numBins; ++bin) {
		if (magnitudes[bin] > EPSILON && magnitudes[bin] > magnitudes[bin - 1] &&
			magnitudes[bin] > magnitudes[bin + 1]) {
			peaks.push_back(bin);
		}
	}
	size_t peakIndex = 0;

	std::vector<float> accumulatedMag(numBins, 0.0f);
	std::vector<float> accumulatedPhaseX(numBins, 0.0f);
	std::vector<float> accumulatedPhaseY(numBins, 0.0f);
	std::vector<float> accumulatedWeight(numBins, 0.0f);
	std::vector<bool> movableSource(numBins, false);
	std::vector<std::complex<float>> shiftedSpectrum(numBins);
	std::vector<bool> shiftedContribution(numBins, false);
	std::array<float, INTERPOLATION_RADIUS * 2> kernel{};
	size_t cachedPhaseBin = numBins;
	int integerShift = 0;
	const float centreStep = std::numbers::pi_v<float> *
		(static_cast<float>(fftSize) - 1.0f) / static_cast<float>(fftSize);

	for (size_t srcBin = 1; srcBin < numBins; ++srcBin) {
		const float mag = magnitudes[srcBin];
		const float expectedFreq = freqResolution * static_cast<float>(srcBin);
		const float decodedFreq = decodedFrequencies[srcBin];
		if (!isUsableFrequencyMapping(expectedFreq, decodedFreq, mag)) {
			continue;
		}
		movableSource[srcBin] = true;
		if (decodedFreq > maximumFrequency) {
			continue;
		}

		while (peakIndex + 1 < peaks.size() &&
			srcBin > (peaks[peakIndex] + peaks[peakIndex + 1]) / 2) {
			++peakIndex;
		}
		const size_t phaseBin = !peaks.empty() &&
			std::abs(shiftRatios[srcBin] - 1.0f) > SHIFT_DETECTION_THRESHOLD &&
			std::abs(shiftRatios[peaks[peakIndex]] - 1.0f) > SHIFT_DETECTION_THRESHOLD
			? peaks[peakIndex] : srcBin;

		const float ratio = shiftRatios[srcBin];
		const float phase = phases[srcBin] + phaseState.offsets[phaseBin];
		if (ratio > 1.0f) {
			if (cachedPhaseBin != phaseBin) {
				const float shift = instantaneousBins[phaseBin] * (shiftRatios[phaseBin] - 1.0f);
				integerShift = static_cast<int>(std::floor(shift));
				const float fraction = shift - static_cast<float>(integerShift);
				kernel = interpolationKernel(fraction);
				cachedPhaseBin = phaseBin;
			}
			const auto value = std::polar(mag, phase + centreStep * static_cast<float>(srcBin));
			for (size_t tap = 0; tap < kernel.size(); ++tap) {
				const auto target = static_cast<ptrdiff_t>(srcBin) + integerShift +
					static_cast<int>(tap) - INTERPOLATION_RADIUS + 1;
				if (target <= 0 || target >= static_cast<ptrdiff_t>(numBins) || kernel[tap] == 0.0f) {
					continue;
				}
				const auto bin = static_cast<size_t>(target);
				shiftedSpectrum[bin] += value * kernel[tap];
				shiftedContribution[bin] = true;
			}
			continue;
		}
		const float targetBinF = static_cast<float>(srcBin) * ratio;
		if (targetBinF < 0.5f || targetBinF >= static_cast<float>(numBins) - 0.5f) {
			continue;
		}
		const size_t targetBinLow = static_cast<size_t>(std::floor(targetBinF));
		const size_t targetBinHigh = targetBinLow + 1;
		const float fracHigh = targetBinF - static_cast<float>(targetBinLow);
		const float fracLow = 1.0f - fracHigh;

		const auto accumulate = [&](size_t targetBin, float fraction) {
			if (targetBin == 0 || targetBin >= numBins || fraction <= 0.0f) {
				return;
			}
			const float centrePhase = std::numbers::pi_v<float> *
				(static_cast<float>(fftSize) - 1.0f) / static_cast<float>(fftSize) *
				(static_cast<float>(srcBin) - static_cast<float>(targetBin));
			const float weight = mag * fraction;
			accumulatedMag[targetBin] += weight;
			accumulatedPhaseX[targetBin] += std::cos(phase + centrePhase) * weight;
			accumulatedPhaseY[targetBin] += std::sin(phase + centrePhase) * weight;
			accumulatedWeight[targetBin] += fraction;
		};
		accumulate(targetBinLow, fracLow);
		accumulate(targetBinHigh, fracHigh);
	}

	result.magnitudes[0] = magnitudes[0];
	result.phases[0] = phases[0];

	for (size_t bin = 1; bin < numBins; ++bin) {
		if (shiftedContribution[bin]) {
			auto value = shiftedSpectrum[bin] * std::polar(1.0f, -centreStep * static_cast<float>(bin));
			if (accumulatedWeight[bin] > EPSILON) {
				value += std::polar(accumulatedMag[bin],
					std::atan2(accumulatedPhaseY[bin], accumulatedPhaseX[bin]));
			}
			result.magnitudes[bin] = std::abs(value);
			result.phases[bin] = std::arg(value);
		} else if (accumulatedWeight[bin] > EPSILON) {
			result.magnitudes[bin] = accumulatedMag[bin];

			const float avgPhaseX = accumulatedPhaseX[bin];
			const float avgPhaseY = accumulatedPhaseY[bin];
			result.phases[bin] = std::atan2(avgPhaseY, avgPhaseX);
		} else if (!movableSource[bin]) {
			result.magnitudes[bin] = magnitudes[bin];
			result.phases[bin] = phases[bin];
		}
	}

	return result;
}

}

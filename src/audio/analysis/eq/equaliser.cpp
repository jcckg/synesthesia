#include "equaliser.h"

#include <algorithm>
#include <cmath>

Equaliser::Equaliser() : lowGain(1.0f), midGain(1.0f), highGain(1.0f) {}

void Equaliser::setGains(const float low, const float mid, const float high) {
	std::lock_guard lock(gainsMutex);
	lowGain = std::max(0.0f, low);
	midGain = std::max(0.0f, mid);
	highGain = std::max(0.0f, high);
}

void Equaliser::getGains(float& low, float& mid, float& high) const {
	std::lock_guard lock(gainsMutex);
	low = lowGain;
	mid = midGain;
	high = highGain;
}

// IEC 61672-1:2013 - Electroacoustics - Sound level meters - Part 1: Specifications
// A-weighting frequency response for perceptual loudness
// Pole frequencies: f1=20.6 Hz, f2=107.7 Hz, f3=737.9 Hz, f4=12194 Hz
// Normalisation: +2.0 dB as specified in IEC 61672-1
// https://en.wikipedia.org/wiki/A-weighting
float Equaliser::calculateAWeighting(const float frequency) {
	const float f2 = frequency * frequency;
	const float numerator = 12194.0f * 12194.0f * f2 * f2;
	const float denominator = (f2 + 20.6f * 20.6f) *
							  std::sqrt((f2 + 107.7f * 107.7f) * (f2 + 737.9f * 737.9f)) *
							  (f2 + 12194.0f * 12194.0f);

	const float aWeight = numerator / denominator;
	const float dbAdjustment = 20.0f * std::log10(aWeight) + 2.0f;
	return std::exp(dbAdjustment * 0.11512925f);
}

void Equaliser::calculateBandResponses(const float frequency, float& lowResponse,
									   float& midResponse, float& highResponse) {
	lowResponse = std::clamp(1.0f - std::max(0.0f, (frequency - LOW_CROSSOVER) / LOW_TRANSITION),
							 0.0f, 1.0f);
	highResponse = std::clamp((frequency - HIGH_CROSSOVER) / HIGH_TRANSITION, 0.0f, 1.0f);
	midResponse = std::clamp(1.0f - lowResponse - highResponse, 0.0f, 1.0f);
}

void Equaliser::applyEQ(std::vector<float>& magnitudes, const float sampleRate,
						const size_t fftSize) const {
	float currentLowGain, currentMidGain, currentHighGain;
	{
		std::lock_guard lock(gainsMutex);
		currentLowGain = lowGain;
		currentMidGain = midGain;
		currentHighGain = highGain;
	}

	for (size_t i = 0; i < magnitudes.size(); ++i) {
		const float frequency = static_cast<float>(i) * sampleRate / static_cast<float>(fftSize);

		float lowResponse, midResponse, highResponse;
		calculateBandResponses(frequency, lowResponse, midResponse, highResponse);

		const float perceptualGain = calculateAWeighting(frequency);

		float combinedGain = perceptualGain * (lowResponse * currentLowGain +
											   midResponse * currentMidGain +
											   highResponse * currentHighGain);

		combinedGain = std::clamp(combinedGain, 0.0f, 4.0f);
		magnitudes[i] *= combinedGain;
	}
}

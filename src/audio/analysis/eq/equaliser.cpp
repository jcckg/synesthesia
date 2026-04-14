#include "equaliser.h"

#include <algorithm>

#include "shared_eq_model.h"

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

void Equaliser::applyEQ(std::vector<float>& magnitudes, const float sampleRate,
						const size_t fftSize) const {
	float currentLowGain, currentMidGain, currentHighGain;
	{
		std::lock_guard lock(gainsMutex);
		currentLowGain = lowGain;
		currentMidGain = midGain;
		currentHighGain = highGain;
	}

	AudioEQ::applyMagnitudeResponse(
		magnitudes,
		sampleRate,
		fftSize,
		currentLowGain,
		currentMidGain,
		currentHighGain);
}

#include "dc_filter.h"

#include <algorithm>

DCFilter::DCFilter(const float alpha)
	: alpha(alpha), previousInputs(1, 0.0f), previousOutputs(1, 0.0f) {}

void DCFilter::setAlpha(const float newAlpha) { this->alpha = std::clamp(newAlpha, 0.0f, 1.0f); }

float DCFilter::process(const float sample, const size_t channel) {
	if (channel >= previousInputs.size()) {
		return sample;
	}

	const float filtered =
		sample - previousInputs[channel] + alpha * previousOutputs[channel];
	previousInputs[channel] = sample;
	previousOutputs[channel] = filtered;

	return filtered;
}

void DCFilter::processBuffer(float* buffer, const size_t numSamples, const size_t channel) {
	if (!buffer || channel >= previousInputs.size()) {
		return;
	}

	for (size_t i = 0; i < numSamples; ++i) {
		buffer[i] = process(buffer[i], channel);
	}
}

void DCFilter::setChannelCount(const size_t channels) {
	previousInputs.resize(channels, 0.0f);
	previousOutputs.resize(channels, 0.0f);
}

void DCFilter::reset() {
	std::ranges::fill(previousInputs, 0.0f);
	std::ranges::fill(previousOutputs, 0.0f);
}

#include "noise_gate.h"

NoiseGate::NoiseGate(const float threshold) : threshold(threshold) {}

void NoiseGate::setThreshold(const float newThreshold) { this->threshold = newThreshold; }

float NoiseGate::process(const float sample) const {
	return std::abs(sample) < threshold ? 0.0f : sample;
}

void NoiseGate::processBuffer(float* buffer, const size_t numSamples) const {
	if (!buffer) {
		return;
	}

	for (size_t i = 0; i < numSamples; ++i) {
		buffer[i] = process(buffer[i]);
	}
}

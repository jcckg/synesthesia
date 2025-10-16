#pragma once

#include <cmath>

class NoiseGate {
public:
	explicit NoiseGate(float threshold = 0.0001f);

	void setThreshold(float threshold);
	float getThreshold() const { return threshold; }

	float process(float sample) const;
	void processBuffer(float* buffer, size_t numSamples) const;

private:
	float threshold;
};

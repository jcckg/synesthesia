#pragma once

#include <vector>

class DCFilter {
public:
	explicit DCFilter(float alpha = 0.995f);

	void setAlpha(float alpha);
	float getAlpha() const { return alpha; }

	float process(float sample, size_t channel = 0);
	void processBuffer(float* buffer, size_t numSamples, size_t channel = 0);

	void setChannelCount(size_t channels);
	void reset();

private:
	float alpha;
	std::vector<float> previousInputs;
	std::vector<float> previousOutputs;
};

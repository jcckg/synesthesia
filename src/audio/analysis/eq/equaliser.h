#pragma once

#include <mutex>
#include <vector>

class Equaliser {
public:
	Equaliser();

	void setGains(float low, float mid, float high);
	void getGains(float& low, float& mid, float& high) const;

	void applyEQ(std::vector<float>& magnitudes, float sampleRate, size_t fftSize) const;

private:
	float lowGain;
	float midGain;
	float highGain;
	mutable std::mutex gainsMutex;
};

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

	static constexpr float LOW_CROSSOVER = 200.0f;
	static constexpr float HIGH_CROSSOVER = 1900.0f;
	static constexpr float LOW_TRANSITION = 50.0f;
	static constexpr float HIGH_TRANSITION = 100.0f;

	static float calculateAWeighting(float frequency);
	static void calculateBandResponses(float frequency, float& lowResponse, float& midResponse,
									   float& highResponse);
};

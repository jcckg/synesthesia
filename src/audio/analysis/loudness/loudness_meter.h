#pragma once

#include <array>
#include <span>
#include <vector>

class LoudnessMeter {
public:
	LoudnessMeter();

	void processSamples(std::span<const float> samples, float sampleRate, std::span<float> weightedSamples = {});
	float getMomentaryLoudness() const;

	static float combineChannelLoudness(std::span<const float> channelLoudness,
                                       std::span<const float> channelWeights = {});

	void reset();

private:
	struct BiquadFilter {
		std::array<double, 3> b = {0.0, 0.0, 0.0};
		std::array<double, 3> a = {1.0, 0.0, 0.0};
		std::array<double, 2> z = {0.0, 0.0};

		double process(double input);
		void reset();
	};

	void initialiseFilters(float sampleRate);
	float loudnessFromMeanSquare(double meanSquare) const;

	BiquadFilter preFilter;
	BiquadFilter rlbFilter;

	static constexpr float BLOCK_DURATION = 0.4f;

	float currentSampleRate;
	size_t blockSize;
	std::vector<double> audioBuffer;
    double runningEnergy = 0.0;
	size_t bufferPosition;
};

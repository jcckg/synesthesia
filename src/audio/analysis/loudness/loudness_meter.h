#pragma once

#include <array>
#include <deque>
#include <span>
#include <vector>
#include <cstdint>

class LoudnessMeter {
public:
	LoudnessMeter();

	void processSamples(std::span<const float> samples, float sampleRate);
	float getIntegratedLoudness() const;
	float getMomentaryLoudness() const;
    float getMaximumMomentaryLoudness() const;

	static float combineChannelLoudness(std::span<const float> channelLoudness,
                                       std::span<const float> channelWeights = {});
	uint64_t getProcessedBlockCount() const { return processedBlockCount; }
	size_t getBlockSizeSamples() const { return blockSize; }
	size_t getBlockHopSamples() const { return hopSize; }
	bool getBlockLoudness(uint64_t index, float& out) const;

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

	std::deque<std::pair<uint64_t, float>> blockHistory;

	static constexpr float ABSOLUTE_THRESHOLD = -70.0f;
	static constexpr float RELATIVE_THRESHOLD = -10.0f;
	static constexpr float BLOCK_DURATION = 0.4f;
	static constexpr float OVERLAP = 0.75f;

	float currentSampleRate;
	size_t blockSize;
	size_t hopSize;
	std::vector<double> audioBuffer;
    double runningEnergy = 0.0;
    double maximumMeanSquare = 0.0;
    std::uint64_t sampleCount = 0;
	size_t bufferPosition;
	uint64_t processedBlockCount{0};
	bool bufferInitialised{false};
	size_t samplesSinceLastBlock{0};
};

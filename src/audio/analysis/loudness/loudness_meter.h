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
	uint64_t getProcessedBlockCount() const { return processedBlockCount; }
	size_t getBlockSizeSamples() const { return blockSize; }
	size_t getBlockHopSamples() const { return hopSize; }
	bool getBlockLoudness(uint64_t index, float& out) const;

	void reset();

private:
	struct BiquadFilter {
		std::array<float, 3> b = {0.0f, 0.0f, 0.0f};
		std::array<float, 3> a = {1.0f, 0.0f, 0.0f};
		std::array<float, 2> z = {0.0f, 0.0f};

		float process(float input);
		void reset();
	};

	void initialiseFilters(float sampleRate);
	float calculateMeanSquare(std::span<const float> samples) const;
	float loudnessFromMeanSquare(float meanSquare) const;

	BiquadFilter preFilter;
	BiquadFilter rlbFilter;

	std::vector<float> filteredSamples;
	std::vector<float> blockLoudness;
	std::deque<std::pair<uint64_t, float>> blockHistory;

	// ITU-R BS.1770-4 gating thresholds for integrated loudness measurement
	// Absolute gate: -70 LUFS (removes silent/very quiet blocks)
	// Relative gate: -10 LU below mean loudness (removes quiet background)
	static constexpr float ABSOLUTE_THRESHOLD = -70.0f;
	static constexpr float RELATIVE_THRESHOLD = -10.0f;
	static constexpr float BLOCK_DURATION = 0.4f;  // 400ms measurement blocks
	static constexpr float OVERLAP = 0.75f;         // 75% overlap (100ms hop size)

	float currentSampleRate;
	size_t blockSize;
	size_t hopSize;
	std::vector<float> audioBuffer;
	size_t bufferPosition;
	uint64_t processedBlockCount{0};
	bool bufferInitialised{false};
	size_t samplesSinceLastBlock{0};
};

#include "loudness_meter.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

LoudnessMeter::LoudnessMeter()
	: currentSampleRate(48000.0f),
	  blockSize(static_cast<size_t>(BLOCK_DURATION * currentSampleRate)),
	  hopSize(static_cast<size_t>(blockSize * (1.0f - OVERLAP))),
	  bufferPosition(0) {
	audioBuffer.resize(blockSize, 0.0f);
	filteredSamples.reserve(blockSize);
	initialiseFilters(currentSampleRate);
}

// ITU-R BS.1770-4 - Algorithms to measure audio programme loudness and true-peak audio level
// Initialises K-weighting filters for perceptual loudness measurement
// Stage 1: High-frequency shelving filter (models head diffraction, ~4 dB boost above 2 kHz)
// Stage 2: High-pass filter at 38 Hz (removes subsonic content)
// https://www.itu.int/rec/R-REC-BS.1770
void LoudnessMeter::initialiseFilters(const float sampleRate) {
	if (sampleRate == currentSampleRate)
		return;

	currentSampleRate = sampleRate;
	blockSize = static_cast<size_t>(BLOCK_DURATION * sampleRate);
	hopSize = static_cast<size_t>(blockSize * (1.0f - OVERLAP));
	audioBuffer.resize(blockSize, 0.0f);

	// ITU-R BS.1770-4 Stage 1: High-frequency shelving filter
	// Centre frequency f0 ≈ 1681.97 Hz, Gain G ≈ 4.0 dB, Q ≈ 0.7071
	const float f0 = 1681.974450955533f;
	const float G = 3.999843853973347f;
	const float Q = 0.7071752369554196f;

	const float K = std::tan(std::numbers::pi_v<float> * f0 / sampleRate);
	const float Vh = std::pow(10.0f, G / 20.0f);
	const float Vb = std::pow(Vh, 0.4996667741545416f);

	const float a0 = 1.0f + K / Q + K * K;
	preFilter.b[0] = (Vh + Vb * K / Q + K * K) / a0;
	preFilter.b[1] = 2.0f * (K * K - Vh) / a0;
	preFilter.b[2] = (Vh - Vb * K / Q + K * K) / a0;
	preFilter.a[0] = 1.0f;
	preFilter.a[1] = 2.0f * (K * K - 1.0f) / a0;
	preFilter.a[2] = (1.0f - K / Q + K * K) / a0;

	// ITU-R BS.1770-4 Stage 2: High-pass filter (RLB - Revised Low-frequency B-weighting)
	// Corner frequency f_hp ≈ 38.14 Hz, Q ≈ 0.5003
	const float f_hp = 38.13547087602444f;
	const float Q_hp = 0.5003270373238773f;
	const float K_hp = std::tan(std::numbers::pi_v<float> * f_hp / sampleRate);

	const float a0_hp = 1.0f + K_hp / Q_hp + K_hp * K_hp;
	rlbFilter.b[0] = 1.0f / a0_hp;
	rlbFilter.b[1] = -2.0f / a0_hp;
	rlbFilter.b[2] = 1.0f / a0_hp;
	rlbFilter.a[0] = 1.0f;
	rlbFilter.a[1] = 2.0f * (K_hp * K_hp - 1.0f) / a0_hp;
	rlbFilter.a[2] = (1.0f - K_hp / Q_hp + K_hp * K_hp) / a0_hp;

	preFilter.reset();
	rlbFilter.reset();
}

float LoudnessMeter::BiquadFilter::process(const float input) {
	const float output = b[0] * input + z[0];
	z[0] = b[1] * input - a[1] * output + z[1];
	z[1] = b[2] * input - a[2] * output;
	return output;
}

void LoudnessMeter::BiquadFilter::reset() {
	z[0] = 0.0f;
	z[1] = 0.0f;
}

void LoudnessMeter::processSamples(const std::span<const float> samples, const float sampleRate) {
	if (samples.empty())
		return;

	initialiseFilters(sampleRate);

	for (const float sample : samples) {
		const float filtered1 = preFilter.process(sample);
		const float filtered2 = rlbFilter.process(filtered1);

		audioBuffer[bufferPosition] = filtered2;
		bufferPosition = (bufferPosition + 1) % blockSize;

		if (bufferPosition % hopSize == 0) {
			const float meanSquare = calculateMeanSquare(audioBuffer);
			const float loudness = loudnessFromMeanSquare(meanSquare);
			blockLoudness.push_back(loudness);

			constexpr size_t maxBlocks = 100;
			if (blockLoudness.size() > maxBlocks) {
				blockLoudness.erase(blockLoudness.begin());
			}
		}
	}
}

float LoudnessMeter::calculateMeanSquare(const std::span<const float> samples) const {
	if (samples.empty())
		return 0.0f;

	const float sum = std::accumulate(samples.begin(), samples.end(), 0.0f,
									  [](const float acc, const float val) { return acc + val * val; });
	return sum / static_cast<float>(samples.size());
}

// ITU-R BS.1770-4 loudness calculation from mean square
// Converts filtered mean square power to LUFS (Loudness Units relative to Full Scale)
// Formula: LUFS = -0.691 + 10 * log10(mean_square)
// The -0.691 offset normalises the scale so that a 1 kHz sine wave at -3.01 dBFS = 0 LUFS
float LoudnessMeter::loudnessFromMeanSquare(const float meanSquare) const {
	if (meanSquare <= 0.0f)
		return -200.0f;

	return -0.691f + 10.0f * std::log10(meanSquare);
}

float LoudnessMeter::getIntegratedLoudness() const {
	if (blockLoudness.empty())
		return -200.0f;

	std::vector<float> gatedBlocks;
	gatedBlocks.reserve(blockLoudness.size());

	for (const float loudness : blockLoudness) {
		if (loudness >= ABSOLUTE_THRESHOLD) {
			gatedBlocks.push_back(loudness);
		}
	}

	if (gatedBlocks.empty())
		return -200.0f;

	std::vector<float> linearValues;
	linearValues.reserve(gatedBlocks.size());
	for (const float loudness : gatedBlocks) {
		linearValues.push_back(std::pow(10.0f, loudness / 10.0f));
	}

	const float meanLinear = std::accumulate(linearValues.begin(), linearValues.end(), 0.0f) /
							 static_cast<float>(linearValues.size());
	const float relativeThreshold = 10.0f * std::log10(meanLinear) + RELATIVE_THRESHOLD;

	std::vector<float> finalGatedLinear;
	finalGatedLinear.reserve(gatedBlocks.size());
	for (size_t i = 0; i < gatedBlocks.size(); ++i) {
		if (gatedBlocks[i] >= relativeThreshold) {
			finalGatedLinear.push_back(linearValues[i]);
		}
	}

	if (finalGatedLinear.empty())
		return -200.0f;

	const float finalMeanLinear = std::accumulate(finalGatedLinear.begin(), finalGatedLinear.end(), 0.0f) /
								  static_cast<float>(finalGatedLinear.size());
	return 10.0f * std::log10(finalMeanLinear);
}

float LoudnessMeter::getMomentaryLoudness() const {
	if (blockLoudness.empty())
		return -200.0f;

	const float lastLoudness = blockLoudness.back();
	return lastLoudness;
}

void LoudnessMeter::reset() {
	preFilter.reset();
	rlbFilter.reset();
	blockLoudness.clear();
	std::ranges::fill(audioBuffer, 0.0f);
	bufferPosition = 0;
}

#include "loudness_meter.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <limits>

LoudnessMeter::LoudnessMeter()
	: currentSampleRate(0.0f),
	  blockSize(static_cast<size_t>(BLOCK_DURATION * 48000.0f)),
	  hopSize(static_cast<size_t>(BLOCK_DURATION * 48000.0f * (1.0f - OVERLAP))),
	  bufferPosition(0) {
	audioBuffer.assign(blockSize, 0.0);
	initialiseFilters(48000.0f);
}

// https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf
void LoudnessMeter::initialiseFilters(const float sampleRate) {
	if (currentSampleRate != 0.0f && sampleRate == currentSampleRate)
		return;
	blockHistory.clear();
	processedBlockCount = 0;

	currentSampleRate = sampleRate;
	blockSize = static_cast<size_t>(BLOCK_DURATION * sampleRate);
	hopSize = blockSize / 4;
	audioBuffer.assign(blockSize, 0.0);
	runningEnergy = 0.0;
    maximumMeanSquare = 0.0;
    sampleCount = 0;
	bufferPosition = 0;
	bufferInitialised = false;
	samplesSinceLastBlock = 0;

	const double f0 = 1681.974450955533;
	const double G = 3.999843853973347;
	const double Q = 0.7071752369554196;

	const double K = std::tan(std::numbers::pi * f0 / sampleRate);
	const double Vh = std::pow(10.0, G / 20.0);
	const double Vb = std::pow(Vh, 0.4996667741545416);

	const double a0 = 1.0 + K / Q + K * K;
	preFilter.b[0] = (Vh + Vb * K / Q + K * K) / a0;
	preFilter.b[1] = 2.0 * (K * K - Vh) / a0;
	preFilter.b[2] = (Vh - Vb * K / Q + K * K) / a0;
	preFilter.a[0] = 1.0;
	preFilter.a[1] = 2.0 * (K * K - 1.0) / a0;
	preFilter.a[2] = (1.0 - K / Q + K * K) / a0;

	// Double precision protects the nearly cancelling high-pass coefficients at high sample rates.
	const double f_hp = 38.13547087602444;
	const double Q_hp = 0.5003270373238773;
	const double K_hp = std::tan(std::numbers::pi * f_hp / sampleRate);

	const double a0_hp = 1.0 + K_hp / Q_hp + K_hp * K_hp;

	const double referenceK = std::tan(std::numbers::pi * f_hp / 48000.0);
	const double referenceA0 = 1.0 + referenceK / Q_hp + referenceK * referenceK;
	const double numeratorGain = referenceA0 / a0_hp;
	rlbFilter.b[0] = numeratorGain;
	rlbFilter.b[1] = -2.0 * numeratorGain;
	rlbFilter.b[2] = numeratorGain;
	rlbFilter.a[0] = 1.0;
	rlbFilter.a[1] = 2.0 * (K_hp * K_hp - 1.0) / a0_hp;
	rlbFilter.a[2] = (1.0 - K_hp / Q_hp + K_hp * K_hp) / a0_hp;

	preFilter.reset();
	rlbFilter.reset();
}

double LoudnessMeter::BiquadFilter::process(const double input) {
	const double output = b[0] * input + z[0];
	z[0] = b[1] * input - a[1] * output + z[1];
	z[1] = b[2] * input - a[2] * output;
	return output;
}

void LoudnessMeter::BiquadFilter::reset() {
	z[0] = 0.0;
	z[1] = 0.0;
}

void LoudnessMeter::processSamples(const std::span<const float> samples, const float sampleRate) {
	if (samples.empty() || !std::isfinite(sampleRate) || sampleRate < 8000.0f)
		return;

	initialiseFilters(sampleRate);

	for (const float sample : samples) {
		const double filtered1 = preFilter.process(std::isfinite(sample) ? sample : 0.0f);
		const double filtered2 = rlbFilter.process(filtered1);

        const double energy = filtered2 * filtered2;
        runningEnergy += energy - audioBuffer[bufferPosition];
        runningEnergy = std::max(0.0, runningEnergy);
        audioBuffer[bufferPosition] = energy;
		bufferPosition = (bufferPosition + 1) % blockSize;
        ++sampleCount;
        const double meanSquare = runningEnergy / static_cast<double>(blockSize);
        maximumMeanSquare = std::max(maximumMeanSquare, meanSquare);
        bufferInitialised = sampleCount >= blockSize;
        ++samplesSinceLastBlock;
        if (bufferInitialised && samplesSinceLastBlock >= hopSize) {
			const float loudness = loudnessFromMeanSquare(meanSquare);
			blockHistory.emplace_back(processedBlockCount, loudness);
			++processedBlockCount;
			samplesSinceLastBlock = 0;
		}
	}
}

float LoudnessMeter::loudnessFromMeanSquare(const double meanSquare) const {
	if (meanSquare <= 0.0f)
		return -200.0f;

	return static_cast<float>(-0.691 + 10.0 * std::log10(meanSquare));
}

float LoudnessMeter::getIntegratedLoudness() const {
	if (blockHistory.empty())
		return -200.0f;

	std::vector<float> gatedBlocks;
	gatedBlocks.reserve(blockHistory.size());

	for (const auto& [_, loudness] : blockHistory) {
		if (loudness > ABSOLUTE_THRESHOLD) {
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
		if (gatedBlocks[i] > relativeThreshold) {
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
    return loudnessFromMeanSquare(runningEnergy / static_cast<double>(blockSize));
}

// https://tech.ebu.ch/docs/tech/tech3341.pdf
float LoudnessMeter::getMaximumMomentaryLoudness() const {
    return loudnessFromMeanSquare(maximumMeanSquare);
}

// https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf
float LoudnessMeter::combineChannelLoudness(const std::span<const float> channelLoudness,
                                           const std::span<const float> channelWeights) {
	if ((!channelWeights.empty() && channelWeights.size() != channelLoudness.size()) ||
        (channelWeights.empty() && channelLoudness.size() > 2)) return std::numeric_limits<float>::quiet_NaN();
	double power = 0.0;
	for (size_t i = 0; i < channelLoudness.size(); ++i) {
        const float loudness = channelLoudness[i];
        const double weight = channelWeights.empty() ? 1.0 : static_cast<double>(channelWeights[i]);
        if (!std::isfinite(weight) || weight < 0.0) return std::numeric_limits<float>::quiet_NaN();
		if (std::isfinite(loudness) && loudness > -200.0f) {
			power += weight * std::pow(10.0, static_cast<double>(loudness) / 10.0);
		}
	}
	return power > 0.0 ? static_cast<float>(10.0 * std::log10(power)) : -200.0f;
}

bool LoudnessMeter::getBlockLoudness(const uint64_t index, float& out) const {
	for (const auto& [blockIndex, loudness] : blockHistory) {
		if (blockIndex == index) {
			out = loudness;
			return true;
		}
	}
	return false;
}

void LoudnessMeter::reset() {
	preFilter.reset();
	rlbFilter.reset();
	blockHistory.clear();
	std::fill(audioBuffer.begin(), audioBuffer.end(), 0.0f);
	bufferPosition = 0;
	bufferInitialised = false;
	samplesSinceLastBlock = 0;
	processedBlockCount = 0;
    runningEnergy = 0.0;
    maximumMeanSquare = 0.0;
    sampleCount = 0;
}

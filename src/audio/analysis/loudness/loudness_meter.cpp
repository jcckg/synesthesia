#include "loudness_meter.h"

#include <algorithm>
#include <cmath>
#include "k_weighting.h"
#include <limits>

LoudnessMeter::LoudnessMeter()
	: currentSampleRate(0.0f),
	  blockSize(static_cast<size_t>(BLOCK_DURATION * 48000.0f)),
	  bufferPosition(0) {
	audioBuffer.assign(blockSize, 0.0);
	initialiseFilters(48000.0f);
}

// https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf
void LoudnessMeter::initialiseFilters(const float sampleRate) {
	if (currentSampleRate != 0.0f && sampleRate == currentSampleRate)
		return;

	currentSampleRate = sampleRate;
	blockSize = static_cast<size_t>(BLOCK_DURATION * sampleRate);
	audioBuffer.assign(blockSize, 0.0);
	runningEnergy = 0.0;
	bufferPosition = 0;

    const auto sections = AudioAnalysis::kWeighting(sampleRate);
    preFilter.b = sections[0].b;
    preFilter.a = sections[0].a;
    rlbFilter.b = sections[1].b;
    rlbFilter.a = sections[1].a;

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

void LoudnessMeter::processSamples(const std::span<const float> samples, const float sampleRate, const std::span<float> weightedSamples) {
	if (samples.empty() || !std::isfinite(sampleRate) || sampleRate < 8000.0f)
		return;

	initialiseFilters(sampleRate);

	for (std::size_t i = 0; i < samples.size(); ++i) {
        const float sample = samples[i];
		const double filtered1 = preFilter.process(std::isfinite(sample) ? sample : 0.0f);
		const double filtered2 = rlbFilter.process(filtered1);
        if (i < weightedSamples.size()) weightedSamples[i] = static_cast<float>(filtered2);

        const double energy = filtered2 * filtered2;
        runningEnergy += energy - audioBuffer[bufferPosition];
        runningEnergy = std::max(0.0, runningEnergy);
        audioBuffer[bufferPosition] = energy;
		bufferPosition = (bufferPosition + 1) % blockSize;

	}
}

float LoudnessMeter::loudnessFromMeanSquare(const double meanSquare) const {
	if (meanSquare <= 0.0f)
		return -200.0f;

	return static_cast<float>(-0.691 + 10.0 * std::log10(meanSquare));
}

float LoudnessMeter::getMomentaryLoudness() const {
    return loudnessFromMeanSquare(runningEnergy / static_cast<double>(blockSize));
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

void LoudnessMeter::reset() {
	preFilter.reset();
	rlbFilter.reset();
	std::fill(audioBuffer.begin(), audioBuffer.end(), 0.0f);
	bufferPosition = 0;
    runningEnergy = 0.0;
}

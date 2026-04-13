#include "resyne/recorder/loudness_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "audio/analysis/fft/fft_processor.h"
#include "audio/analysis/loudness/loudness_meter.h"
#include "colour/colour_core.h"
#include "constants.h"
#include "resyne/encoding/audio/wav_encoder.h"

namespace ReSyne::LoudnessUtils {

namespace {

constexpr float INVALID_LOUDNESS_LUFS = -200.0f;
constexpr float BS1770_LUFS_OFFSET = -0.691f;

float loudnessToMeanSquare(const float loudness) {
	if (!std::isfinite(loudness) || loudness <= INVALID_LOUDNESS_LUFS) {
		return 0.0f;
	}
	return std::pow(10.0f, (loudness - BS1770_LUFS_OFFSET) / 10.0f);
}

float meanSquareToLoudness(const float meanSquare) {
	if (!(meanSquare > 0.0f) || !std::isfinite(meanSquare)) {
		return INVALID_LOUDNESS_LUFS;
	}
	return BS1770_LUFS_OFFSET + 10.0f * std::log10(meanSquare);
}

std::vector<std::vector<float>> deinterleaveChannels(const std::vector<float>& interleavedSamples,
													  const size_t numChannels) {
	std::vector<std::vector<float>> channels(numChannels);
	if (numChannels == 0) {
		return channels;
	}

	const size_t frames = interleavedSamples.size() / numChannels;
	for (auto& channel : channels) {
		channel.resize(frames, 0.0f);
	}

	for (size_t frame = 0; frame < frames; ++frame) {
		for (size_t ch = 0; ch < numChannels; ++ch) {
			channels[ch][frame] = interleavedSamples[frame * numChannels + ch];
		}
	}

	return channels;
}

}

void calculateLoudnessFromSpectralFrames(std::vector<AudioColourSample>& samples,
										  const AudioMetadata& metadata) {
	if (samples.empty() || metadata.sampleRate <= 0.0f) {
		return;
	}

	const int fftSize = metadata.fftSize > 0 ? metadata.fftSize : FFTProcessor::FFT_SIZE;
	const int hopSize = metadata.hopSize > 0 ? metadata.hopSize : (fftSize / 2);

	const uint32_t numChannels = !samples.empty() ? samples.front().channels : 1;

	std::vector<SpectralSample> spectralSamples;
	spectralSamples.reserve(samples.size());

	for (const auto& sample : samples) {
		SpectralSample spectral;
		spectral.timestamp = sample.timestamp;
		spectral.sampleRate = sample.sampleRate > 0.0f ? sample.sampleRate : metadata.sampleRate;
		spectral.magnitudes.resize(numChannels);
		spectral.phases.resize(numChannels);
		spectral.frequencies.resize(numChannels);

		for (uint32_t ch = 0; ch < numChannels; ++ch) {
			if (ch < sample.magnitudes.size()) {
				spectral.magnitudes[ch] = sample.magnitudes[ch];
			}
			if (ch < sample.phases.size()) {
				spectral.phases[ch] = sample.phases[ch];
			}
			if (ch < sample.frequencies.size()) {
				spectral.frequencies[ch] = sample.frequencies[ch];
			}
		}

		spectralSamples.push_back(std::move(spectral));
	}

	const auto reconstructionResult = WAVEncoder::reconstructFromSpectralData(
		spectralSamples, metadata.sampleRate, fftSize, hopSize);

	if (!reconstructionResult.success || reconstructionResult.audioSamples.empty()) {
		for (auto& sample : samples) {
			if (!std::isfinite(sample.loudnessLUFS)) {
				sample.loudnessLUFS = ColourCore::LOUDNESS_DB_UNSPECIFIED;
				sample.splDb = std::numeric_limits<float>::quiet_NaN();
			}
		}
		return;
	}

	std::vector<float> blockLoudness;
	size_t blockHopSamples = 0;
	size_t blockSizeSamples = 0;

	if (reconstructionResult.numChannels <= 1) {
		LoudnessMeter loudnessMeter;
		loudnessMeter.processSamples(reconstructionResult.audioSamples, metadata.sampleRate);

		const uint64_t processedBlockCount = loudnessMeter.getProcessedBlockCount();
		blockHopSamples = loudnessMeter.getBlockHopSamples();
		blockSizeSamples = loudnessMeter.getBlockSizeSamples();
		blockLoudness.resize(static_cast<size_t>(processedBlockCount), INVALID_LOUDNESS_LUFS);

		for (uint64_t blockIndex = 0; blockIndex < processedBlockCount; ++blockIndex) {
			float loudness = INVALID_LOUDNESS_LUFS;
			if (loudnessMeter.getBlockLoudness(blockIndex, loudness)) {
				blockLoudness[static_cast<size_t>(blockIndex)] = loudness;
			}
		}
	} else {
		const auto channelSamples = deinterleaveChannels(
			reconstructionResult.audioSamples,
			reconstructionResult.numChannels);

		std::vector<LoudnessMeter> channelMeters(reconstructionResult.numChannels);
		uint64_t processedBlockCount = std::numeric_limits<uint64_t>::max();

		for (size_t ch = 0; ch < channelSamples.size(); ++ch) {
			channelMeters[ch].processSamples(channelSamples[ch], metadata.sampleRate);
			processedBlockCount = std::min(processedBlockCount, channelMeters[ch].getProcessedBlockCount());
		}

		if (processedBlockCount == std::numeric_limits<uint64_t>::max()) {
			processedBlockCount = 0;
		}

		if (!channelMeters.empty()) {
			blockHopSamples = channelMeters.front().getBlockHopSamples();
			blockSizeSamples = channelMeters.front().getBlockSizeSamples();
		}

		blockLoudness.resize(static_cast<size_t>(processedBlockCount), INVALID_LOUDNESS_LUFS);

		for (uint64_t blockIndex = 0; blockIndex < processedBlockCount; ++blockIndex) {
			float combinedMeanSquare = 0.0f;
			for (auto& meter : channelMeters) {
				float channelLoudness = INVALID_LOUDNESS_LUFS;
				if (meter.getBlockLoudness(blockIndex, channelLoudness)) {
					// Channel layout is not stored in spectral assets, so use equal weights.
					combinedMeanSquare += loudnessToMeanSquare(channelLoudness);
				}
			}
			blockLoudness[static_cast<size_t>(blockIndex)] = meanSquareToLoudness(combinedMeanSquare);
		}
	}

	const uint64_t processedBlockCount = static_cast<uint64_t>(blockLoudness.size());

	for (size_t frameIndex = 0; frameIndex < samples.size(); ++frameIndex) {
		auto& sample = samples[frameIndex];

		const bool hasValidLoudness = std::isfinite(sample.loudnessLUFS) &&
		                               std::isnormal(sample.loudnessLUFS) &&
		                               sample.loudnessLUFS > -200.0f &&
		                               sample.loudnessLUFS < 20.0f;

		if (hasValidLoudness) {
			if (!std::isfinite(sample.splDb)) {
				sample.splDb = sample.loudnessLUFS + synesthesia::constants::REFERENCE_SPL_AT_0_LUFS;
			}
			continue;
		}

		const int64_t frameCentreSample = static_cast<int64_t>(frameIndex * static_cast<size_t>(hopSize) + static_cast<size_t>(hopSize) / 2);

		uint64_t closestBlockIndex = 0;
		int64_t minDistance = std::numeric_limits<int64_t>::max();

		for (uint64_t blockIndex = 0; blockIndex < processedBlockCount; ++blockIndex) {
			const int64_t blockCentreSample = static_cast<int64_t>(blockIndex * blockHopSamples + blockSizeSamples / 2);
			const int64_t distance = std::abs(frameCentreSample - blockCentreSample);

			if (distance < minDistance) {
				minDistance = distance;
				closestBlockIndex = blockIndex;
			}
		}

		float loudness = INVALID_LOUDNESS_LUFS;
		if (processedBlockCount > 0) {
			loudness = blockLoudness[static_cast<size_t>(closestBlockIndex)];
		}
		if (processedBlockCount > 0 && std::isfinite(loudness) && loudness > INVALID_LOUDNESS_LUFS) {
			sample.loudnessLUFS = loudness;
			sample.splDb = loudness + synesthesia::constants::REFERENCE_SPL_AT_0_LUFS;
		} else {
			sample.loudnessLUFS = ColourCore::LOUDNESS_DB_UNSPECIFIED;
			sample.splDb = std::numeric_limits<float>::quiet_NaN();
		}
	}
}

}

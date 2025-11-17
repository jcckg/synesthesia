#include "resyne/recorder/loudness_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "audio/analysis/loudness/loudness_meter.h"
#include "colour/colour_mapper.h"
#include "constants.h"
#include "resyne/encoding/audio/wav_encoder.h"

namespace ReSyne::LoudnessUtils {

// ITU-R BS.1770-4: Algorithms to measure audio programme loudness and true-peak audio level
// Calculates loudness from spectral frames by reconstructing time-domain audio via IFFT
// and processing through K-weighted loudness meter with dual-gate algorithm
void calculateLoudnessFromSpectralFrames(std::vector<AudioColourSample>& samples,
										  const AudioMetadata& metadata) {
	if (samples.empty() || metadata.sampleRate <= 0.0f) {
		return;
	}

	const int fftSize = metadata.fftSize > 0 ? metadata.fftSize : 2048;
	const int hopSize = metadata.hopSize > 0 ? metadata.hopSize : (fftSize / 2);

	std::vector<SpectralSample> spectralSamples;
	spectralSamples.reserve(samples.size());

	for (const auto& sample : samples) {
		SpectralSample spectral;
		spectral.magnitudes = sample.magnitudes;
		spectral.phases = sample.phases;
		spectral.timestamp = sample.timestamp;
		spectral.sampleRate = sample.sampleRate;
		spectralSamples.push_back(std::move(spectral));
	}

	const auto reconstructionResult = WAVEncoder::reconstructFromSpectralData(
		spectralSamples, metadata.sampleRate, fftSize, hopSize);

	if (!reconstructionResult.success || reconstructionResult.audioSamples.empty()) {
		for (auto& sample : samples) {
			if (!std::isfinite(sample.loudnessLUFS)) {
				sample.loudnessLUFS = ColourMapper::LOUDNESS_DB_UNSPECIFIED;
				sample.splDb = std::numeric_limits<float>::quiet_NaN();
			}
		}
		return;
	}

	LoudnessMeter loudnessMeter;
	loudnessMeter.processSamples(reconstructionResult.audioSamples, metadata.sampleRate);

	const uint64_t processedBlockCount = loudnessMeter.getProcessedBlockCount();
	const size_t blockHopSamples = loudnessMeter.getBlockHopSamples();
	const size_t blockSizeSamples = loudnessMeter.getBlockSizeSamples();

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

		float loudness = -200.0f;
		if (processedBlockCount > 0 && loudnessMeter.getBlockLoudness(closestBlockIndex, loudness)) {
			sample.loudnessLUFS = loudness;
			sample.splDb = loudness + synesthesia::constants::REFERENCE_SPL_AT_0_LUFS;
		} else {
			sample.loudnessLUFS = ColourMapper::LOUDNESS_DB_UNSPECIFIED;
			sample.splDb = std::numeric_limits<float>::quiet_NaN();
		}
	}
}

}

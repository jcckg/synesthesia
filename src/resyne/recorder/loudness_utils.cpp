#include "resyne/recorder/loudness_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "audio/analysis/loudness/loudness_meter.h"
#include "colour/colour_mapper.h"
#include "constants.h"
#include "resyne/encoding/audio/wav_encoder.h"

namespace ReSyne::LoudnessUtils {

void calculateLoudnessFromSpectralFrames(std::vector<AudioColourSample>& samples,
										  const AudioMetadata& metadata) {
	if (samples.empty() || metadata.sampleRate <= 0.0f) {
		return;
	}

	const int fftSize = metadata.fftSize > 0 ? metadata.fftSize : 2048;
	const int hopSize = metadata.hopSize > 0 ? metadata.hopSize : (fftSize / 2);

	const uint32_t numChannels = !samples.empty() ? samples.front().channels : 1;

	std::vector<SpectralSample> spectralSamples;
	spectralSamples.reserve(samples.size());

	for (const auto& sample : samples) {
		SpectralSample spectral;
		spectral.timestamp = sample.timestamp;
		spectral.sampleRate = sample.sampleRate;

		if (numChannels == 1 || sample.magnitudes.size() <= 1) {
			spectral.magnitudes.clear();
			spectral.phases.clear();
			if (!sample.magnitudes.empty()) {
				spectral.magnitudes.push_back(sample.magnitudes[0]);
			} else {
				spectral.magnitudes.push_back(std::vector<float>());
			}
			if (!sample.phases.empty()) {
				spectral.phases.push_back(sample.phases[0]);
			} else {
				spectral.phases.push_back(std::vector<float>());
			}
		} else {
			const size_t numBins = !sample.magnitudes.empty() && !sample.magnitudes[0].empty()
				? sample.magnitudes[0].size() : 0;

			if (numBins > 0) {
				std::vector<float> avgMagnitudes(numBins, 0.0f);
				std::vector<float> avgPhases(numBins, 0.0f);

				for (uint32_t ch = 0; ch < numChannels && ch < sample.magnitudes.size(); ++ch) {
					if (sample.magnitudes[ch].size() == numBins) {
						for (size_t bin = 0; bin < numBins; ++bin) {
							const float mag = sample.magnitudes[ch][bin];
							avgMagnitudes[bin] += mag * mag;
						}
					}
				}

				for (uint32_t ch = 0; ch < numChannels && ch < sample.phases.size(); ++ch) {
					if (sample.phases[ch].size() == numBins) {
						for (size_t bin = 0; bin < numBins; ++bin) {
							avgPhases[bin] += sample.phases[ch][bin];
						}
					}
				}

				const float invChannels = 1.0f / static_cast<float>(numChannels);
				for (size_t bin = 0; bin < numBins; ++bin) {
					avgMagnitudes[bin] = std::sqrt(avgMagnitudes[bin] * invChannels);
					avgPhases[bin] *= invChannels;
				}

				spectral.magnitudes.push_back(std::move(avgMagnitudes));
				spectral.phases.push_back(std::move(avgPhases));
			}
		}

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

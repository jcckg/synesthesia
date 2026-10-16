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
										  const AudioMetadata& metadata, const bool replaceExisting) {
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

	// Rebuild analysis at the source level; the audio-export peak guard would alter full-scale loudness.
	const auto reconstructionResult = WAVEncoder::reconstructFromSpectralData(
		spectralSamples, metadata.sampleRate, fftSize, hopSize, metadata.analysisLeadingSamples, metadata.originalSampleCount, false);

	if (!reconstructionResult.success || reconstructionResult.audioSamples.empty()) {
		for (auto& sample : samples) {
			if (!std::isfinite(sample.loudnessLUFS)) {
				sample.loudnessLUFS = ColourCore::LOUDNESS_DB_UNSPECIFIED;
				sample.splDb = std::numeric_limits<float>::quiet_NaN();
			}
		}
		return;
	}

    const auto channels = deinterleaveChannels(reconstructionResult.audioSamples, reconstructionResult.numChannels);
    std::vector<LoudnessMeter> meters(channels.size());
    std::vector<AudioAnalysis::BassAnalyser> bass(channels.size());
    std::size_t processedSamples = 0;
    for (auto& sample : samples) {
        const double availableTime = sample.timestamp + static_cast<double>(fftSize) * .5 / metadata.sampleRate;
        const double position = std::isfinite(availableTime) ? availableTime * metadata.sampleRate : 0.0;
        const auto end = static_cast<std::size_t>(std::clamp(std::round(position),
            static_cast<double>(processedSamples), static_cast<double>(channels.front().size())));
        for (std::size_t ch = 0; ch < channels.size(); ++ch) {
            meters[ch].processSamples(std::span<const float>(channels[ch]).subspan(processedSamples, end - processedSamples),
                metadata.sampleRate);
            bass[ch].process(std::span<const float>(channels[ch]).subspan(processedSamples,end-processedSamples),metadata.sampleRate);
        }
        sample.bass.resize(channels.size());
        for (std::size_t ch = 0; ch < channels.size(); ++ch) sample.bass[ch] = bass[ch].spectrum();
        processedSamples = end;
        const bool valid = std::isfinite(sample.loudnessLUFS) && sample.loudnessLUFS > -200.0f && sample.loudnessLUFS < 20.0f;
        if (valid && !replaceExisting) continue;
        std::vector<float> levels;
        levels.reserve(meters.size());
        for (const auto& meter : meters) levels.push_back(meter.getMomentaryLoudness());
        sample.loudnessLUFS = LoudnessMeter::combineChannelLoudness(levels);
        sample.splDb = std::numeric_limits<float>::quiet_NaN();
    }
}

}

#include "resyne/recorder/loudness_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

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
										  const AudioMetadata& metadata, const bool replaceExisting,
                                          const std::function<void(float)>& progress) {
	if (samples.empty() || metadata.sampleRate <= 0.0f) {
		return;
	}

	const int fftSize = metadata.fftSize > 0 ? metadata.fftSize : FFTProcessor::FFT_SIZE;
	const int hopSize = metadata.hopSize > 0 ? metadata.hopSize : (fftSize / 2);

	const uint32_t numChannels = !samples.empty() ? samples.front().channels : 1;

	std::vector<SpectralSample> spectralSamples;
	spectralSamples.reserve(samples.size());

	for (const auto& sample : samples) {
        if (progress) progress(0.1f * static_cast<float>(spectralSamples.size()) / static_cast<float>(samples.size()));
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
		spectralSamples, metadata.sampleRate, fftSize, hopSize, metadata.analysisLeadingSamples, metadata.originalSampleCount, false,
        [&](float fraction) { if (progress) progress(0.1f + 0.5f * fraction); });

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
    AudioAnalysis::RealFFT transform(static_cast<std::size_t>(fftSize));
    std::vector<float> hann(static_cast<std::size_t>(fftSize)), window(hann.size()), filtered;
    std::vector<kiss_fft_cpx> bins(hann.size() / 2 + 1);
    std::vector<std::vector<float>> weightedWindows(channels.size(), std::vector<float>(hann.size()));
    for (std::size_t i = 0; i < hann.size(); ++i)
        hann[i] = .5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / (fftSize - 1)));
    std::size_t processedSamples = 0, previousEnd = 0;
    std::size_t completed = 0;
    for (auto& sample : samples) {
        if (progress) progress(0.6f + 0.4f * static_cast<float>(completed++) / static_cast<float>(samples.size()));
        const double availableTime = sample.timestamp + static_cast<double>(fftSize) * .5 / metadata.sampleRate;
        const double position = std::isfinite(availableTime) ? availableTime * metadata.sampleRate : 0.0;
        const auto end = static_cast<std::size_t>(std::clamp(std::round(position),
            static_cast<double>(processedSamples), static_cast<double>(channels.front().size())));
        const auto logicalEnd = static_cast<std::size_t>(std::clamp(std::round(position),
            static_cast<double>(previousEnd), static_cast<double>(channels.front().size() + fftSize)));
        sample.kWeightedMagnitudes.resize(channels.size());
        for (std::size_t ch = 0; ch < channels.size(); ++ch) {
            filtered.resize(end - processedSamples);
            meters[ch].processSamples(std::span<const float>(channels[ch]).subspan(processedSamples, end - processedSamples),
                metadata.sampleRate, filtered);
            auto& weighted = weightedWindows[ch];
            if (filtered.size() >= weighted.size())
                std::copy(filtered.end() - static_cast<std::ptrdiff_t>(weighted.size()), filtered.end(), weighted.begin());
            else if (!filtered.empty()) {
                std::move(weighted.begin() + static_cast<std::ptrdiff_t>(filtered.size()), weighted.end(), weighted.begin());
                std::copy(filtered.begin(), filtered.end(), weighted.end() - static_cast<std::ptrdiff_t>(filtered.size()));
            }
            const auto padding = std::min(weighted.size(), logicalEnd - previousEnd - filtered.size());
            if (padding > 0) {
                std::move(weighted.begin() + static_cast<std::ptrdiff_t>(padding), weighted.end(), weighted.begin());
                std::fill(weighted.end() - static_cast<std::ptrdiff_t>(padding), weighted.end(), 0.0f);
            }
            for (std::size_t i = 0; i < window.size(); ++i) window[i] = weighted[i] * hann[i];
            transform.forward(window, bins);
            auto& magnitudes = sample.kWeightedMagnitudes[ch];
            magnitudes.resize(bins.size());
            for (std::size_t i = 0; i < bins.size(); ++i)
                magnitudes[i] = std::hypot(bins[i].r, bins[i].i) * (i == 0 || i + 1 == bins.size() ? 1.0f : 2.0f) / fftSize;
            bass[ch].process(std::span<const float>(channels[ch]).subspan(processedSamples,end-processedSamples),metadata.sampleRate);
        }
        sample.bass.resize(channels.size());
        for (std::size_t ch = 0; ch < channels.size(); ++ch) sample.bass[ch] = bass[ch].spectrum();
        processedSamples = end;
        previousEnd = logicalEnd;
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

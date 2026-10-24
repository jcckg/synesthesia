#include "resyne/encoding/audio/wav_encoder.h"
#include "resyne/encoding/reconstruction/spectral_resampling.h"
#include "audio/analysis/fft/real_fft.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <numbers>

namespace {

constexpr float TARGET_PEAK = 0.98f;

void applyPeakGuard(std::vector<float>& samples, const std::function<void(float)>& progress) {
	if (samples.empty()) {
		return;
	}

	float peak = 0.0f;
	for (size_t i = 0; i < samples.size(); ++i) {
        if (progress && (i % 4096) == 0) progress(0.5f * static_cast<float>(i) / static_cast<float>(samples.size()));
		peak = std::max(peak, std::fabs(samples[i]));
	}

	if (peak <= std::numeric_limits<float>::epsilon()) {
		return;
	}

	if (peak <= TARGET_PEAK) {
		return;
	}

	const float scale = TARGET_PEAK / peak;
	for (size_t i = 0; i < samples.size(); ++i) {
        if (progress && (i % 4096) == 0) progress(0.5f + 0.5f * static_cast<float>(i) / static_cast<float>(samples.size()));
		samples[i] *= scale;
	}
}

}

WAVEncoder::EncodingResult WAVEncoder::reconstructFromSpectralData(
	const std::vector<SpectralSample>& samples,
	float sampleRate,
	int fftSize,
	int hopSize,
    const std::uint32_t leadingSamples,
    const std::uint64_t originalSampleCount,
    const bool limitPeak,
    const std::function<void(float)>& progress
) {
	EncodingResult result;
	result.success = false;
	result.sampleRate = sampleRate;
	result.numChannels = 0;

	if (samples.empty()) {
		result.errorMessage = "No spectral samples provided";
		return result;
	}
	if (fftSize < 2 || (fftSize % 2) != 0 || hopSize <= 0) {
		result.errorMessage = "Invalid FFT or hop size";
		return result;
	}

	size_t numChannels = samples[0].magnitudes.size();

	if (numChannels == 0) {
		result.errorMessage = "Sample has 0 channels";
		return result;
	}
	result.numChannels = numChannels;

	std::vector<std::vector<float>> channelAudio(numChannels);

	for (size_t ch = 0; ch < numChannels; ++ch) {
		std::vector<std::vector<float>> timeFrames;
		timeFrames.reserve(samples.size());
		std::vector<bool> frequencyShifted(samples.size(), false);
		SpectralResampling::PhaseState phaseState;

		for (size_t frame = 0; frame < samples.size(); ++frame) {
            if (progress) progress(0.8f * (static_cast<float>(ch) + 0.75f * static_cast<float>(frame) / static_cast<float>(samples.size())) / static_cast<float>(numChannels));
			const auto& sample = samples[frame];
			if (ch >= sample.magnitudes.size() || ch >= sample.phases.size()) {
				timeFrames.push_back(std::vector<float>(static_cast<size_t>(fftSize), 0.0f));
				phaseState = {};
				continue;
			}

			const auto& mags = sample.magnitudes[ch];
			const auto& phs = sample.phases[ch];

			std::vector<float> frameMagnitudes = mags;
			std::vector<float> framePhases = phs;
			if (ch < sample.frequencies.size() && !sample.frequencies[ch].empty()) {
				auto resampled = SpectralResampling::resampleSpectrum(
					frameMagnitudes,
					framePhases,
					sample.frequencies[ch],
					sampleRate,
					static_cast<size_t>(fftSize),
					hopSize,
					phaseState);
				frameMagnitudes = std::move(resampled.magnitudes);
				framePhases = std::move(resampled.phases);
				frequencyShifted[frame] = resampled.frequencyShifted;
			} else {
				phaseState = {};
			}

			std::vector<float> timeFrame = inverseFFT(frameMagnitudes, framePhases, fftSize);

			timeFrames.push_back(std::move(timeFrame));
		}

		channelAudio[ch] = overlapAdd(timeFrames, hopSize, frequencyShifted, [&](float fraction) {
            if (progress) progress(0.8f * (static_cast<float>(ch) + 0.75f + 0.25f * fraction) / static_cast<float>(numChannels));
        });
        if (leadingSamples > 0 || originalSampleCount > 0) {
            auto& audio = channelAudio[ch];
            if (leadingSamples > audio.size() || originalSampleCount > audio.size() - leadingSamples) {
                result.errorMessage = "Spectral frames do not cover the declared audio extent";
                return result;
            }
            const size_t length = originalSampleCount > 0 ? static_cast<size_t>(originalSampleCount) : audio.size() - leadingSamples;
            std::move(audio.begin() + leadingSamples, audio.begin() + leadingSamples + length, audio.begin());
            audio.resize(length);
        }
	}

	if (numChannels == 1) {
		result.audioSamples = std::move(channelAudio[0]);
	} else {
		size_t numSamplesPerChannel = channelAudio[0].size();
		for (size_t ch = 1; ch < numChannels; ++ch) {
			numSamplesPerChannel = std::max(numSamplesPerChannel, channelAudio[ch].size());
		}

		result.audioSamples.resize(numSamplesPerChannel * numChannels);
		for (size_t i = 0; i < numSamplesPerChannel; ++i) {
            if (progress && (i % 4096) == 0) progress(0.8f + 0.1f * static_cast<float>(i) / static_cast<float>(numSamplesPerChannel));
			for (size_t ch = 0; ch < numChannels; ++ch) {
				result.audioSamples[i * numChannels + ch] =
					i < channelAudio[ch].size() ? channelAudio[ch][i] : 0.0f;
			}
		}
	}

	if (limitPeak) applyPeakGuard(result.audioSamples, [&](float fraction) {
        if (progress) progress(0.9f + 0.1f * fraction);
    });
    if (progress) progress(1.0f);

	result.success = true;

	return result;
}

std::vector<float> WAVEncoder::inverseFFT(
	const std::vector<float>& magnitudes,
	const std::vector<float>& phases,
	int fftSize
) {
	const size_t numBins = static_cast<size_t>(fftSize / 2 + 1);
	std::vector<kiss_fft_cpx> fftBins(numBins, {0.0f, 0.0f});

	const size_t dataSize = std::min(magnitudes.size(), phases.size());

	for (size_t i = 0; i < numBins && i < dataSize; ++i) {
		float magnitude = magnitudes[i];
		float phase = phases[i];

		const bool edgeBin = i == 0 || i + 1 == numBins;
		const float scale = static_cast<float>(fftSize) * (edgeBin ? 1.0f : 0.5f);
		fftBins[i].r = magnitude * scale * std::cos(phase);
		fftBins[i].i = magnitude * scale * std::sin(phase);
	}

    thread_local std::unique_ptr<AudioAnalysis::RealFFT> transform;
    thread_local int transformSize = 0;
    if (!transform || transformSize != fftSize) {
        transform = std::make_unique<AudioAnalysis::RealFFT>(static_cast<std::size_t>(fftSize),AudioAnalysis::FFTBackend::Automatic,true);
        transformSize = fftSize;
    }
    std::vector<float> timeDomain(static_cast<size_t>(fftSize));
    transform->inverse(fftBins,timeDomain);
	const float inverseScale = 1.0f / static_cast<float>(fftSize);
	for (float& value : timeDomain) {
		value *= inverseScale;
	}

	return timeDomain;
}

std::vector<float> WAVEncoder::overlapAdd(
	const std::vector<std::vector<float>>& frames,
	int hopSize,
	const std::vector<bool>& frequencyShifted,
    const std::function<void(float)>& progress
) {
	if (frames.empty()) {
		return {};
	}

	const size_t frameSize = frames[0].size();
	const size_t totalSamples = (frames.size() - 1) * static_cast<size_t>(hopSize) + frameSize;

	if (frameSize == 0) {
		return {};
	}

	std::vector<float> hannWindow(frameSize, 1.0f);
	if (frameSize > 1) {
		const float denom = static_cast<float>(frameSize - 1);
		for (size_t n = 0; n < frameSize; ++n) {
			hannWindow[n] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(n) / denom));
		}
	}

	std::vector<float> output(totalSamples, 0.0f);
	std::vector<float> normalisation(totalSamples, 0.0f);

	for (size_t i = 0; i < frames.size(); ++i) {
        if (progress) progress(0.5f * static_cast<float>(i) / static_cast<float>(frames.size()));
		size_t writePos = i * static_cast<size_t>(hopSize);

		const auto& frame = frames[i];
		const size_t copyCount = std::min(frameSize, frame.size());

		for (size_t j = 0; j < copyCount && (writePos + j) < totalSamples; ++j) {
			const float windowed = frame[j] * hannWindow[j];
			const size_t index = writePos + j;
			output[index] += windowed;
			normalisation[index] += hannWindow[j] * hannWindow[j];
		}
	}

	if (std::any_of(frequencyShifted.begin(), frequencyShifted.end(),
		[](bool shifted) { return shifted; })) {
		const size_t hop = static_cast<size_t>(hopSize);
		std::vector<float> completeNormalisation(std::min(hop, frameSize), 0.0f);
		for (size_t j = 0; j < frameSize; ++j) {
			completeNormalisation[j % hop] += hannWindow[j] * hannWindow[j];
		}
		for (size_t frame = 0; frame < frames.size(); ++frame) {
            if (progress) progress(0.5f + 0.25f * static_cast<float>(frame) / static_cast<float>(frames.size()));
			const size_t writePos = frame * hop;
			if (!frequencyShifted[frame] ||
				(writePos >= frameSize && writePos + frameSize <= totalSamples - frameSize)) {
				continue;
			}
			for (size_t j = 0; j < frames[frame].size() && j < frameSize; ++j) {
				const size_t index = writePos + j;
				normalisation[index] = std::max(normalisation[index], completeNormalisation[j % hop]);
			}
		}
	}

	constexpr float normalisationEpsilon = 1e-6f;
	for (size_t i = 0; i < output.size(); ++i) {
        if (progress && (i % 4096) == 0) progress(0.75f + 0.25f * static_cast<float>(i) / static_cast<float>(output.size()));
		if (normalisation[i] > normalisationEpsilon) {
			output[i] /= normalisation[i];
		}
	}

	return output;
}

bool WAVEncoder::exportToWAV(
	const std::string& wavPath,
	const std::vector<float>& audioSamples,
	float sampleRate,
	size_t numChannels,
    const std::function<void(float)>& progress
) {
	if (audioSamples.empty() || numChannels == 0) {
		return false;
	}

	std::ofstream file(wavPath, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}

	const uint32_t numSamples = static_cast<uint32_t>(audioSamples.size());
	const uint16_t bitsPerSample = 16;
	const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * static_cast<uint32_t>(numChannels) * (bitsPerSample / 8);
	const uint16_t blockAlign = static_cast<uint16_t>(numChannels * (bitsPerSample / 8));
	const uint32_t dataSize = numSamples * static_cast<uint32_t>(sizeof(int16_t));
	const uint32_t fileSize = 36 + dataSize;

	file.write("RIFF", 4);
	file.write(reinterpret_cast<const char*>(&fileSize), 4);
	file.write("WAVE", 4);

	file.write("fmt ", 4);
	uint32_t fmtSize = 16;
	file.write(reinterpret_cast<const char*>(&fmtSize), 4);
	uint16_t audioFormat = 1;
	file.write(reinterpret_cast<const char*>(&audioFormat), 2);
	uint16_t channels = static_cast<uint16_t>(numChannels);
	file.write(reinterpret_cast<const char*>(&channels), 2);
	uint32_t sampleRateInt = static_cast<uint32_t>(sampleRate);
	file.write(reinterpret_cast<const char*>(&sampleRateInt), 4);
	file.write(reinterpret_cast<const char*>(&byteRate), 4);
	file.write(reinterpret_cast<const char*>(&blockAlign), 2);
	file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

	file.write("data", 4);
	file.write(reinterpret_cast<const char*>(&dataSize), 4);

    std::vector<int16_t> intSamples(std::min<size_t>(65536, audioSamples.size()));
    for (size_t offset = 0; offset < audioSamples.size(); offset += intSamples.size()) {
        if (progress) progress(static_cast<float>(offset) / static_cast<float>(audioSamples.size()));
        const auto count = std::min(intSamples.size(), audioSamples.size() - offset);
        for (size_t i = 0; i < count; ++i)
            intSamples[i] = static_cast<int16_t>(std::clamp(audioSamples[offset + i], -1.0f, 1.0f) * 32767.0f);
        file.write(reinterpret_cast<const char*>(intSamples.data()), static_cast<std::streamsize>(count * sizeof(int16_t)));
        if (!file.good()) return false;
    }
    if (progress) progress(1.0f);

	file.close();
	return true;
}

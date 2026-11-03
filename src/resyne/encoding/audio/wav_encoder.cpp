#include "resyne/encoding/audio/wav_encoder.h"
#include "resyne/encoding/formats/exporter.h"
#include "resyne/encoding/reconstruction/spectral_resampling.h"
#include "audio/analysis/fft/real_fft.h"
#include <fstream>
#include "utilities/cancellation.h"
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

template <typename Sample>
WAVEncoder::EncodingResult reconstruct(
	const std::span<const Sample> samples,
	float sampleRate,
	int fftSize,
	int hopSize,
    const std::uint32_t leadingSamples,
    const std::uint64_t originalSampleCount,
    const bool limitPeak,
    const std::function<void(float)>& progress
) {
	WAVEncoder::EncodingResult result;
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

    if (samples.size() - 1 > (std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(fftSize)) / static_cast<std::size_t>(hopSize)) {
        result.errorMessage = "Audio extent exceeds available address space";
        return result;
    }
    const auto size = static_cast<std::size_t>(fftSize);
    const auto hop = static_cast<std::size_t>(hopSize);
    const auto total = (samples.size() - 1) * hop + size;
    std::vector<float> window(size, 1);
    for (std::size_t i = 0; i < size; ++i)
        window[i] = .5f * (1 - std::cos(2 * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(size - 1)));
    AudioAnalysis::RealFFT transform(size, AudioAnalysis::FFTBackend::Automatic, true);
    std::vector<kiss_fft_cpx> bins(size / 2 + 1);
    std::vector<float> timeFrame(size), normalisation(total);
    std::vector<std::vector<float>> channelAudio(numChannels);
    for (std::size_t ch = 0; ch < numChannels; ++ch) {
        auto& output = channelAudio[ch];
        output.assign(total, 0);
        std::fill(normalisation.begin(), normalisation.end(), 0);
        std::vector<bool> frequencyShifted(samples.size(), false);
        SpectralResampling::PhaseState phaseState;
        for (std::size_t frame = 0; frame < samples.size(); ++frame) {
            Utilities::checkCancellation();
            if (progress) progress(.8f * (static_cast<float>(ch) + static_cast<float>(frame) / static_cast<float>(samples.size())) / static_cast<float>(numChannels));
            const auto& sample = samples[frame];
            std::fill(bins.begin(), bins.end(), kiss_fft_cpx{});
            if (ch < sample.magnitudes.size() && ch < sample.phases.size()) {
                const auto* magnitudes = &sample.magnitudes[ch];
                const auto* phases = &sample.phases[ch];
                SpectralResampling::ResampledSpectrum resampled;
                if (ch < sample.frequencies.size() && !sample.frequencies[ch].empty()) {
                    resampled = SpectralResampling::resampleSpectrum(*magnitudes, *phases, sample.frequencies[ch], sampleRate, size, hopSize, phaseState);
                    magnitudes = &resampled.magnitudes;
                    phases = &resampled.phases;
                    frequencyShifted[frame] = resampled.frequencyShifted;
                } else phaseState = {};
                for (std::size_t bin = 0; bin < std::min({bins.size(), magnitudes->size(), phases->size()}); ++bin) {
                    const float scale = static_cast<float>(fftSize) * (bin == 0 || bin + 1 == bins.size() ? 1.0f : .5f);
                    bins[bin].r = (*magnitudes)[bin] * scale * std::cos((*phases)[bin]);
                    bins[bin].i = (*magnitudes)[bin] * scale * std::sin((*phases)[bin]);
                }
            } else phaseState = {};
            transform.inverse(bins, timeFrame);
            const float scale = 1.0f / static_cast<float>(fftSize);
            for (auto& value : timeFrame) value *= scale;
            const auto offset = frame * hop;
            for (std::size_t i = 0; i < size; ++i) {
                output[offset + i] += timeFrame[i] * window[i];
                normalisation[offset + i] += window[i] * window[i];
            }
        }
        if (std::any_of(frequencyShifted.begin(), frequencyShifted.end(), [](bool value) { return value; })) {
            std::vector<float> complete(std::min(hop, size), 0);
            for (std::size_t i = 0; i < size; ++i) complete[i % hop] += window[i] * window[i];
            for (std::size_t frame = 0; frame < samples.size(); ++frame) {
            Utilities::checkCancellation();
                const auto offset = frame * hop;
                if (!frequencyShifted[frame] || (offset >= size && offset + size <= total - size)) continue;
                for (std::size_t i = 0; i < size; ++i)
                    normalisation[offset + i] = std::max(normalisation[offset + i], complete[i % hop]);
            }
        }
        for (std::size_t i = 0; i < total; ++i) {
            if (i % 4096 == 0) Utilities::checkCancellation();
            if (progress && i % 4096 == 0) progress(.8f * (static_cast<float>(ch) + 1) / static_cast<float>(numChannels));
            if (normalisation[i] > 1e-6f) output[i] /= normalisation[i];
        }
        if (leadingSamples > 0 || originalSampleCount > 0) {
            if (leadingSamples > output.size() || originalSampleCount > output.size() - leadingSamples) {
                result.errorMessage = "Spectral frames do not cover the declared audio extent";
                return result;
            }
            const auto length = originalSampleCount > 0 ? static_cast<std::size_t>(originalSampleCount) : output.size() - leadingSamples;
            std::move(output.begin() + leadingSamples, output.begin() + leadingSamples + length, output.begin());
            output.resize(length);
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

WAVEncoder::EncodingResult WAVEncoder::reconstructFromSpectralData(const std::vector<SpectralSample>& samples,
    float rate, int fftSize, int hop, std::uint32_t leading, std::uint64_t count, bool limit,
    const std::function<void(float)>& progress) {
    return reconstruct(std::span<const SpectralSample>(samples), rate, fftSize, hop, leading, count, limit, progress);
}

WAVEncoder::EncodingResult WAVEncoder::reconstructFromSpectralData(std::span<const AudioColourSample> samples,
    float rate, int fftSize, int hop, std::uint32_t leading, std::uint64_t count, bool limit,
    const std::function<void(float)>& progress) {
    return reconstruct(samples, rate, fftSize, hop, leading, count, limit, progress);
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

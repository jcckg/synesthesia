#include "resyne/encoding/audio/wav_encoder.h"
#include <kiss_fftr.h>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <numbers>

class KissFFTRConfig {
public:
	explicit KissFFTRConfig(int fftSize, int inverse)
		: cfg_(kiss_fftr_alloc(fftSize, inverse, nullptr, nullptr)) {}

	~KissFFTRConfig() {
		if (cfg_) {
			kiss_fftr_free(cfg_);
		}
	}

	KissFFTRConfig(const KissFFTRConfig&) = delete;
	KissFFTRConfig& operator=(const KissFFTRConfig&) = delete;

	KissFFTRConfig(KissFFTRConfig&& other) noexcept : cfg_(other.cfg_) {
		other.cfg_ = nullptr;
	}

	KissFFTRConfig& operator=(KissFFTRConfig&& other) noexcept {
		if (this != &other) {
			if (cfg_) {
				kiss_fftr_free(cfg_);
			}
			cfg_ = other.cfg_;
			other.cfg_ = nullptr;
		}
		return *this;
	}

	kiss_fftr_cfg get() const { return cfg_; }
	operator bool() const { return cfg_ != nullptr; }

private:
	kiss_fftr_cfg cfg_;
};

namespace {

constexpr float TARGET_PEAK = 0.9f;

void applyLimiter(std::vector<float>& samples) {
	if (samples.empty()) {
		return;
	}

	float peak = 0.0f;
	for (float value : samples) {
		peak = std::max(peak, std::fabs(value));
	}

	if (peak <= std::numeric_limits<float>::epsilon()) {
		return;
	}

	if (peak <= TARGET_PEAK) {
		return;
	}

	const float scale = TARGET_PEAK / peak;
	for (float& value : samples) {
		value *= scale;
	}
}

}

WAVEncoder::EncodingResult WAVEncoder::reconstructFromSpectralData(
	const std::vector<SpectralSample>& samples,
	float sampleRate,
	int fftSize,
	int hopSize
) {
	EncodingResult result;
	result.success = false;
	result.sampleRate = sampleRate;
	result.numChannels = 1;

	if (samples.empty()) {
		result.errorMessage = "No spectral samples provided";
		return result;
	}

	std::vector<std::vector<float>> timeFrames;
	timeFrames.reserve(samples.size());

	for (const auto& sample : samples) {
		std::vector<float> timeFrame = inverseFFT(sample.magnitudes, sample.phases, fftSize);

		// Energy compensation for FFT scaling convention
		// Parseval's theorem for real DFT: Σ x[n]² = (1/N) [|X[0]|² + 2Σ|X[k]|² + |X[N/2]|²]
		// However, our forward FFT applies 2.0/N scaling to regular bins and 1.0/N to DC/Nyquist
		// The compensation below accounts for this custom scaling to maintain signal energy
		float timeEnergy = 0.0f;
		for (float value : timeFrame) {
			timeEnergy += value * value;
		}

		if (!sample.magnitudes.empty() && fftSize > 0 && timeEnergy > std::numeric_limits<float>::epsilon()) {
			const size_t binCount = sample.magnitudes.size();
			float edgeEnergy = sample.magnitudes[0] * sample.magnitudes[0];
			if (binCount > 1) {
				edgeEnergy += sample.magnitudes[binCount - 1] * sample.magnitudes[binCount - 1];
			}

			float interiorSum = 0.0f;
			for (size_t bin = 1; bin + 1 < binCount; ++bin) {
				const float magnitude = sample.magnitudes[bin];
				interiorSum += magnitude * magnitude;
			}

			// Empirical energy formula compensating for forward FFT 2.0/N scaling
			const float spectralEnergy = static_cast<float>(fftSize) * (edgeEnergy + 0.5f * interiorSum);
			if (spectralEnergy > std::numeric_limits<float>::epsilon()) {
				const float gain = std::sqrt(spectralEnergy / timeEnergy);
				const float clampedGain = std::clamp(gain, 0.1f, 10.0f);
				if (std::isfinite(clampedGain) && std::abs(clampedGain - 1.0f) > 1e-4f) {
					for (float& value : timeFrame) {
						value *= clampedGain;
					}
				}
			}
		}

		timeFrames.push_back(std::move(timeFrame));
	}

	std::vector<float> reconstructed = overlapAdd(timeFrames, hopSize);

	// Preserve all samples including the first frame
	// Previously skipped first hopSize samples to avoid windowing artifacts,
	// but this created audible dead space at the beginning of playback
	applyLimiter(reconstructed);
	result.audioSamples = std::move(reconstructed);

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

	// Reconstruct complex FFT bins from magnitude and phase
	// Forward FFT scaling: regular bins by 2.0/N, DC and Nyquist by 1.0/N (see fft_processor.cpp)
	// KissFFT inverse multiplies by N:
	//   - Regular bins: (2.0/N) * N = 2.0× windowed signal
	//   - DC/Nyquist:   (1.0/N) * N = 1.0× windowed signal
	// Scale DC/Nyquist by 2× to match regular bins for consistent amplitude
	for (size_t i = 0; i < numBins && i < dataSize; ++i) {
		float magnitude = magnitudes[i];
		float phase = phases[i];

		fftBins[i].r = magnitude * std::cos(phase);
		fftBins[i].i = magnitude * std::sin(phase);
	}

	// Compensate for 0.5× extra scaling on DC and Nyquist from forward FFT
	if (!fftBins.empty()) {
		fftBins[0].r *= 2.0f;
		fftBins[0].i *= 2.0f;
		if (numBins > 1) {
			fftBins[numBins - 1].r *= 2.0f;
			fftBins[numBins - 1].i *= 2.0f;
		}
	}

	KissFFTRConfig cfg(fftSize, 1);
	if (!cfg) {
		return std::vector<float>(static_cast<size_t>(fftSize), 0.0f);
	}

	std::vector<float> timeDomain(static_cast<size_t>(fftSize));
	kiss_fftri(cfg.get(), fftBins.data(), timeDomain.data());

	return timeDomain;
}

std::vector<float> WAVEncoder::overlapAdd(
	const std::vector<std::vector<float>>& frames,
	int hopSize
) {
	if (frames.empty()) {
		return {};
	}

	const size_t frameSize = frames[0].size();
	const size_t totalSamples = (frames.size() - 1) * static_cast<size_t>(hopSize) + frameSize;

	if (frameSize == 0) {
		return {};
	}

	// Hann window for WOLA (Weighted Overlap-Add) synthesis
	// With 50% overlap (hop = frameSize/2), Hann² windows satisfy COLA property
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
		size_t writePos = i * static_cast<size_t>(hopSize);

		const auto& frame = frames[i];
		const size_t copyCount = std::min(frameSize, frame.size());

		for (size_t j = 0; j < copyCount && (writePos + j) < totalSamples; ++j) {
			const float windowed = frame[j] * hannWindow[j];
			const size_t index = writePos + j;
			output[index] += windowed;
			// WOLA normalisation: divide by sum of squared windows for perfect reconstruction
			normalisation[index] += hannWindow[j] * hannWindow[j];
		}
	}

	constexpr float normalisationEpsilon = 1e-6f;
	constexpr float expectedColaSumHann50 = 0.375f;

	const size_t edgeStart = static_cast<size_t>(hopSize);
	const size_t edgeEnd = totalSamples - (frameSize - static_cast<size_t>(hopSize));

	for (size_t i = 0; i < output.size(); ++i) {
		if (normalisation[i] > normalisationEpsilon) {
			if (i < edgeStart || i >= edgeEnd) {
				output[i] /= std::max(expectedColaSumHann50, normalisation[i]);
			} else {
				output[i] /= normalisation[i];
			}
		}
	}

	return output;
}

bool WAVEncoder::exportToWAV(
	const std::string& wavPath,
	const std::vector<float>& audioSamples,
	float sampleRate,
	size_t numChannels
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
	const uint32_t dataSize = numSamples * blockAlign;
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

	std::vector<int16_t> intSamples(audioSamples.size());
	for (size_t i = 0; i < audioSamples.size(); ++i) {
		intSamples[i] = static_cast<int16_t>(std::clamp(audioSamples[i], -1.0f, 1.0f) * 32767.0f);
	}
	file.write(reinterpret_cast<const char*>(intSamples.data()), static_cast<std::streamsize>(intSamples.size() * sizeof(int16_t)));

	file.close();
	return true;
}

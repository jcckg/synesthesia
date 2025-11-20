#pragma once

#include <vector>
#include <string>

struct SpectralSample {
	std::vector<std::vector<float>> magnitudes;
	std::vector<std::vector<float>> phases;
	double timestamp;
	float sampleRate;
};

class WAVEncoder {
public:
	struct EncodingResult {
		std::vector<float> audioSamples;
		float sampleRate;
		size_t numChannels;
		bool success;
		std::string errorMessage;
	};

	static EncodingResult reconstructFromSpectralData(
		const std::vector<SpectralSample>& samples,
		float sampleRate,
		int fftSize = 2048,
		int hopSize = 1024
	);

	static bool exportToWAV(
		const std::string& wavPath,
		const std::vector<float>& audioSamples,
		float sampleRate,
		size_t numChannels = 1
	);

	static std::vector<float> inverseFFT(
		const std::vector<float>& magnitudes,
		const std::vector<float>& phases,
		int fftSize
	);

private:
	static std::vector<float> overlapAdd(
		const std::vector<std::vector<float>>& frames,
		int hopSize
	);
};

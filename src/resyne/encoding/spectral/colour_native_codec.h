#pragma once

#include "resyne/encoding/formats/exporter.h"

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

struct RGBAColour {
	float r;
	float g;
	float b;
	float a;
};

struct ColourNativeImage {
	size_t width = 0;
	size_t height = 0;
	std::vector<RGBAColour> pixels;
	AudioMetadata metadata;

	void resize(size_t newWidth, size_t newHeight) {
		width = newWidth;
		height = newHeight;
		pixels.resize(width * height);
	}

	RGBAColour& at(size_t x, size_t y) {
		return pixels[y * width + x];
	}

	const RGBAColour& at(size_t x, size_t y) const {
		return pixels[y * width + x];
	}
};

class ColourNativeCodec {
public:
	static constexpr size_t MAX_BIN_COUNT = 4096;

	static ColourNativeImage encode(const std::vector<AudioColourSample>& samples,
								   const AudioMetadata& metadata,
								   const std::function<void(float)>& onProgress = {});

	static std::vector<AudioColourSample> decode(const ColourNativeImage& image,
												float& sampleRate,
												int& hopSize,
												const SequenceFrameCallback& onFrameDecoded = {},
												const std::function<void(float)>& onProgress = {});

	static float detectSampleRate(const ColourNativeImage& image);

	static void encodeTimeFrame(const std::vector<float>& magnitudes,
								const std::vector<float>& phases,
								const std::vector<float>& frequencies,
								float sampleRate,
								float hopRatio,
								std::vector<RGBAColour>& column);

	static void decodeTimeFrame(const std::vector<RGBAColour>& column,
								float sampleRate,
								std::vector<float>& magnitudes,
								std::vector<float>& phases,
								std::vector<float>& frequencies);

private:
    static RGBAColour encodeFrequencyBin(float frequency,
                                         float magnitude,
                                         float phase);

    static float normaliseMagnitude(float magnitude);
    static float denormaliseMagnitude(float normalised);
    static float normaliseLogFrequency(float frequency);
    static float denormaliseLogFrequency(float normalised);
    static std::pair<float, float> encodePhaseVector(float phase);
    static float decodePhaseVector(float encodedCosine, float encodedSine, bool& valid);
};

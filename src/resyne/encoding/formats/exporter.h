#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

struct AudioColourSample {
	std::vector<float> magnitudes;
	std::vector<float> phases;
	double timestamp;
	float sampleRate;
};

struct AudioMetadata {
    float sampleRate = 0.0f;
    int fftSize = 0;
    int hopSize = 0;
    std::string windowType = "hann";
    size_t numFrames = 0;
    size_t numBins = 0;
    std::string version = "3.0.0";
};

using SequenceFrameCallback = std::function<void(const std::vector<AudioColourSample>&, size_t)>;

class SequenceExporter {
public:
	static bool exportToResyne(const std::string& filepath,
                                const std::vector<AudioColourSample>& samples,
                                const AudioMetadata& metadata,
                                const std::function<void(float)>& progress = {});

	static bool loadFromResyne(const std::string& filepath,
							   std::vector<AudioColourSample>& samples,
							   AudioMetadata& metadata,
							   const std::function<void(float)>& progress = {},
							   const SequenceFrameCallback& onFrameDecoded = {});

	static bool exportToSynesthesia(const std::string& filepath,
							   const std::vector<AudioColourSample>& samples,
							   const AudioMetadata& metadata,
							   const std::function<void(float)>& progress = {}) {
		return exportToResyne(filepath, samples, metadata, progress);
	}

	static bool loadFromSynesthesia(const std::string& filepath,
							   std::vector<AudioColourSample>& samples,
							   AudioMetadata& metadata) {
		return loadFromResyne(filepath, samples, metadata);
	}

	static bool exportToWAV(const std::string& filepath,
						   const std::vector<AudioColourSample>& samples,
						   const AudioMetadata& metadata,
						   const std::function<void(float)>& progress = {});

	static bool exportToTIFF(const std::string& filepath,
							const std::vector<AudioColourSample>& samples,
							const AudioMetadata& metadata,
							const std::function<void(float)>& progress = {});

	static bool loadFromTIFF(const std::string& filepath,
							std::vector<AudioColourSample>& samples,
							AudioMetadata& metadata,
							const std::function<void(float)>& progress = {},
							const SequenceFrameCallback& onFrameDecoded = {});
};

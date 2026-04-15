#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

#include "resyne/encoding/formats/rsyn_asset.h"

struct AudioColourSample {
	std::vector<std::vector<float>> magnitudes;
	std::vector<std::vector<float>> phases;
	std::vector<std::vector<float>> frequencies;
	double timestamp;
	float sampleRate;
	float loudnessLUFS = std::numeric_limits<float>::quiet_NaN();
	float splDb = std::numeric_limits<float>::quiet_NaN();
	std::uint32_t channels = 1;
};

struct AudioMetadata {
    float sampleRate = 0.0f;
    int fftSize = 0;
    int hopSize = 0;
    double durationSeconds = 0.0;
    std::string windowType = "hann";
    size_t numFrames = 0;
    size_t numBins = 0;
    std::uint32_t channels = 1;
    std::string version = "3.0.0";
    std::shared_ptr<RSYNSourceData> sourceData;
    std::shared_ptr<RSYNPresentationData> presentationData;
    std::shared_ptr<RSYNLazyAsset> lazyAsset;
};

struct RSYNExportOptions {
    RSYNPresentationSettings presentationSettings{};
};

using SequenceFrameCallback = std::function<void(const std::vector<AudioColourSample>&, size_t)>;

class SequenceExporter {
public:
    static bool exportToRsyn(const std::string& filepath,
                             const std::vector<AudioColourSample>& samples,
                             const AudioMetadata& metadata,
                             const RSYNExportOptions& options,
                             const std::function<void(float)>& progress = {});

    static bool loadFromRsyn(const std::string& filepath,
                             std::vector<AudioColourSample>& samples,
                             AudioMetadata& metadata,
                             const std::function<void(float)>& progress = {},
                             const SequenceFrameCallback& onFrameDecoded = {});

    static bool loadFromRsynShell(const std::string& filepath,
                                  AudioMetadata& metadata,
                                  const std::function<void(float)>& progress = {});

    static bool hydrateRsynSamples(AudioMetadata& metadata,
                                   std::vector<AudioColourSample>& samples,
                                   const std::function<void(float)>& progress = {},
                                   const SequenceFrameCallback& onFrameDecoded = {});

    static bool hydrateRsynSource(AudioMetadata& metadata,
                                  const std::function<void(float)>& progress = {});

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

#pragma once

#include <string>
#include <vector>
#include <functional>
#include "resyne/encoding/formats/exporter.h"
#include "colour/colour_mapper.h"

class FFTProcessor;
class Equaliser;

namespace ReSyne::ImportHelpers {

using ProgressCallback = std::function<void(float)>;

using StatusCallback = std::function<void(const std::string&)>;

using PreviewCallback = std::function<void(const std::vector<AudioColourSample>&)>;

bool importAudioFile(
    const std::string& filepath,
    ColourMapper::ColourSpace colourSpace,
    bool applyGamutMapping,
    int analysisHopSize,
    float importLowGain,
    float importMidGain,
    float importHighGain,
    std::vector<AudioColourSample>& samples,
    AudioMetadata& metadata,
    std::string& errorMessage,
    const ProgressCallback& onProgress = nullptr,
    const PreviewCallback& onPreview = nullptr,
    bool enableSmoothing = true,
    bool enableMelWeighting = true,
    std::vector<float>* playbackAudio = nullptr
);

bool importResyneFile(
    const std::string& filepath,
    ColourMapper::ColourSpace colourSpace,
    bool applyGamutMapping,
    float fallbackSampleRate,
    float importLowGain,
    float importMidGain,
    float importHighGain,
    std::vector<AudioColourSample>& samples,
    AudioMetadata& metadata,
    std::string& errorMessage,
    const ProgressCallback& onProgress = nullptr,
    const PreviewCallback& onPreview = nullptr
);

}

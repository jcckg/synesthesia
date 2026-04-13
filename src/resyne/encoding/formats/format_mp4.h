#pragma once

#include <functional>
#include <string>
#include <vector>

#include "colour/colour_core.h"
#include "resyne/encoding/formats/exporter.h"

namespace ReSyne::Encoding::Video {

struct ExportOptions {
    std::string ffmpegExecutable;
    ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020;
    bool applyGamutMapping = true;
    float smoothingAmount = 0.6f;
    int width = 1920;
    int height = 1080;
    int frameRate = 60;
    bool exportGradient = false;
};

bool exportToMP4(const std::string& outputPath,
                 const std::vector<AudioColourSample>& samples,
                 const AudioMetadata& metadata,
                 const ExportOptions& options,
                 const std::function<void(float)>& progress,
                 std::string& errorMessage);

}

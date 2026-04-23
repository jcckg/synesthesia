#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "cli.h"
#include "resyne/encoding/formats/exporter.h"

namespace CLI::Misc {

enum class PresentationTrack {
    Auto,
    Smoothed,
    Analysis
};

struct PresentationSample {
    float labL = 0.0f;
    float labA = 0.0f;
    float labB = 0.0f;
    float displayR = 0.0f;
    float displayG = 0.0f;
    float displayB = 0.0f;
    float linearRenderR = 0.0f;
    float linearRenderG = 0.0f;
    float linearRenderB = 0.0f;
    float loudnessDb = 0.0f;
    float loudnessNormalised = 0.0f;
    double timestamp = 0.0;
};

struct LoadedPresentation {
    std::filesystem::path inputPath;
    AudioMetadata metadata{};
    PresentationTrack requestedTrack = PresentationTrack::Auto;
    PresentationTrack resolvedTrack = PresentationTrack::Analysis;
};

bool parsePresentationTrack(const std::string& value, PresentationTrack& track);
const char* presentationTrackName(PresentationTrack track);

bool isRsynPath(const std::filesystem::path& path);
bool isAudioPath(const std::filesystem::path& path);

bool loadPresentationForInput(const Arguments& args,
                              LoadedPresentation& loaded,
                              std::string& errorMessage);

bool collectPresentationSamples(const LoadedPresentation& loaded,
                                std::vector<PresentationSample>& samples);

double resolveFrameDurationSeconds(const AudioMetadata& metadata,
                                   const std::vector<PresentationSample>& samples,
                                   std::size_t sampleIndex);

std::string colourSpaceName(ColourCore::ColourSpace colourSpace);

}

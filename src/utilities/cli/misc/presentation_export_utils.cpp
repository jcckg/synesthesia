#include "misc/presentation_export_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "audio/analysis/fft/fft_processor.h"
#include "colour/colour_core.h"
#include "colour/colour_presentation.h"
#include "resyne/encoding/formats/rsyn_presentation.h"
#include "resyne/recorder/import_helpers.h"

namespace fs = std::filesystem;

namespace CLI::Misc {

namespace {

constexpr float kNeutralEqGain = 1.0f;

const std::vector<std::string> kAudioExtensions = {
    ".wav", ".flac", ".mp3", ".mpeg3", ".mpga", ".ogg", ".oga"
};

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

PresentationTrack resolvePresentationTrack(const RSYNPresentationData& presentation,
                                           const PresentationTrack requestedTrack) {
    if (requestedTrack != PresentationTrack::Auto) {
        return requestedTrack;
    }

    return presentation.settings.smoothingEnabled
        ? PresentationTrack::Smoothed
        : PresentationTrack::Analysis;
}

bool buildPresentationFromSamples(const std::vector<AudioColourSample>& spectralSamples,
                                  AudioMetadata& metadata,
                                  const bool disableSmoothing) {
    RSYNPresentationSettings settings{};
    if (metadata.presentationData != nullptr) {
        settings = metadata.presentationData->settings;
    }
    if (disableSmoothing) {
        settings.smoothingEnabled = false;
        settings.manualSmoothing = false;
    }

    metadata.presentationData = RSYNPresentation::buildPresentationData(
        spectralSamples,
        settings);
    return metadata.presentationData != nullptr && !metadata.presentationData->frames.empty();
}

void buildLinearRenderColour(const float labL,
                             const float labA,
                             const float labB,
                             float& linearR,
                             float& linearG,
                             float& linearB) {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    ColourCore::LabtoXYZ(labL, labA, labB, X, Y, Z);
    ColourCore::XYZtoRGB(
        X,
        Y,
        Z,
        linearR,
        linearG,
        linearB,
        ColourCore::ColourSpace::SRGB,
        false,
        true);
    ColourPresentation::applyOutputPrecision(linearR, linearG, linearB);
}

}

bool parsePresentationTrack(const std::string& value, PresentationTrack& track) {
    const std::string lowered = toLower(value);
    if (lowered == "auto") {
        track = PresentationTrack::Auto;
        return true;
    }
    if (lowered == "smoothed") {
        track = PresentationTrack::Smoothed;
        return true;
    }
    if (lowered == "analysis") {
        track = PresentationTrack::Analysis;
        return true;
    }
    return false;
}

const char* presentationTrackName(const PresentationTrack track) {
    switch (track) {
        case PresentationTrack::Smoothed:
            return "smoothed";
        case PresentationTrack::Analysis:
            return "analysis";
        case PresentationTrack::Auto:
        default:
            return "auto";
    }
}

bool isRsynPath(const fs::path& path) {
    return toLower(path.extension().string()) == ".rsyn";
}

bool isAudioPath(const fs::path& path) {
    const std::string extension = toLower(path.extension().string());
    return std::find(kAudioExtensions.begin(), kAudioExtensions.end(), extension) != kAudioExtensions.end();
}

bool loadPresentationForInput(const Arguments& args,
                              LoadedPresentation& loaded,
                              std::string& errorMessage) {
    loaded = LoadedPresentation{};

    if (args.inputDir.empty()) {
        errorMessage = "missing input path";
        return false;
    }

    loaded.inputPath = fs::path(args.inputDir);
    if (!parsePresentationTrack(args.miscTrack, loaded.requestedTrack)) {
        errorMessage = "--misc-track must be 'auto', 'smoothed', or 'analysis'";
        return false;
    }
    if (args.disableSmoothing && loaded.requestedTrack == PresentationTrack::Smoothed) {
        errorMessage = "--disable-smoothing cannot be combined with --misc-track smoothed";
        return false;
    }

    if (isRsynPath(loaded.inputPath)) {
        if (!SequenceExporter::loadFromRsynShell(loaded.inputPath.string(), loaded.metadata)) {
            errorMessage = "failed to load RSYN metadata/presentation";
            return false;
        }

        if (loaded.metadata.presentationData == nullptr || loaded.metadata.presentationData->frames.empty()) {
            std::vector<AudioColourSample> spectralSamples;
            if (!SequenceExporter::hydrateRsynSamples(loaded.metadata, spectralSamples) ||
                !buildPresentationFromSamples(spectralSamples, loaded.metadata, args.disableSmoothing)) {
                errorMessage = "failed to build presentation data from RSYN samples";
                return false;
            }
        }
    } else if (isAudioPath(loaded.inputPath)) {
        std::vector<AudioColourSample> spectralSamples;
        if (!ReSyne::ImportHelpers::importAudioFile(
                loaded.inputPath.string(),
                ColourCore::ColourSpace::Rec2020,
                true,
                std::clamp(args.analysisHop, 1, FFTProcessor::FFT_SIZE),
                kNeutralEqGain,
                kNeutralEqGain,
                kNeutralEqGain,
                spectralSamples,
                loaded.metadata,
                errorMessage,
                nullptr,
                nullptr)) {
            if (errorMessage.empty()) {
                errorMessage = "failed to analyse audio input";
            }
            return false;
        }

        if (!buildPresentationFromSamples(spectralSamples, loaded.metadata, args.disableSmoothing)) {
            errorMessage = "failed to build presentation data from audio input";
            return false;
        }
    } else {
        errorMessage = "unsupported input format";
        return false;
    }

    if (loaded.metadata.presentationData == nullptr || loaded.metadata.presentationData->frames.empty()) {
        errorMessage = "input has no presentation frames";
        return false;
    }

    loaded.resolvedTrack = args.disableSmoothing
        ? PresentationTrack::Analysis
        : resolvePresentationTrack(*loaded.metadata.presentationData, loaded.requestedTrack);
    return true;
}

bool collectPresentationSamples(const LoadedPresentation& loaded,
                                std::vector<PresentationSample>& samples) {
    samples.clear();

    if (loaded.metadata.presentationData == nullptr || loaded.metadata.presentationData->frames.empty()) {
        return false;
    }

    const auto& presentation = *loaded.metadata.presentationData;
    samples.reserve(presentation.frames.size());

    for (const auto& frame : presentation.frames) {
        PresentationSample sample{};
        sample.timestamp = frame.timestamp;
        sample.loudnessDb = frame.analysis.frameLoudnessDb;
        sample.loudnessNormalised = frame.analysis.loudnessNormalised;

        if (loaded.resolvedTrack == PresentationTrack::Smoothed) {
            sample.labL = frame.smoothedLab[0];
            sample.labA = frame.smoothedLab[1];
            sample.labB = frame.smoothedLab[2];
            sample.displayR = frame.smoothedDisplayRgb[0];
            sample.displayG = frame.smoothedDisplayRgb[1];
            sample.displayB = frame.smoothedDisplayRgb[2];
        } else {
            sample.labL = frame.analysis.L;
            sample.labA = frame.analysis.a;
            sample.labB = frame.analysis.b_comp;
            sample.displayR = frame.analysis.r;
            sample.displayG = frame.analysis.g;
            sample.displayB = frame.analysis.b;
        }

        ColourPresentation::applyOutputPrecision(
            sample.displayR,
            sample.displayG,
            sample.displayB);
        buildLinearRenderColour(
            sample.labL,
            sample.labA,
            sample.labB,
            sample.linearRenderR,
            sample.linearRenderG,
            sample.linearRenderB);
        samples.push_back(sample);
    }

    return !samples.empty();
}

double resolveFrameDurationSeconds(const AudioMetadata& metadata,
                                   const std::vector<PresentationSample>& samples,
                                   const std::size_t sampleIndex) {
    if (sampleIndex + 1 < samples.size()) {
        const double delta = samples[sampleIndex + 1].timestamp - samples[sampleIndex].timestamp;
        if (std::isfinite(delta) && delta > 0.0) {
            return delta;
        }
    }

    if (sampleIndex > 0 && sampleIndex < samples.size()) {
        const double delta = samples[sampleIndex].timestamp - samples[sampleIndex - 1].timestamp;
        if (std::isfinite(delta) && delta > 0.0) {
            return delta;
        }
    }

    if (metadata.sampleRate > 0.0f && metadata.hopSize > 0) {
        return static_cast<double>(metadata.hopSize) / static_cast<double>(metadata.sampleRate);
    }

    if (metadata.durationSeconds > 0.0 && !samples.empty()) {
        return metadata.durationSeconds / static_cast<double>(samples.size());
    }

    return 1.0;
}

std::string colourSpaceName(const ColourCore::ColourSpace colourSpace) {
    switch (colourSpace) {
        case ColourCore::ColourSpace::DisplayP3:
            return "Display P3";
        case ColourCore::ColourSpace::SRGB:
            return "sRGB";
        case ColourCore::ColourSpace::Rec2020:
        default:
            return "Rec.2020";
    }
}

}

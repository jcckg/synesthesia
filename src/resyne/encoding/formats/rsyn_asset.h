#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "colour/colour_core.h"
#include "resyne/encoding/formats/rsyn_container.h"

inline constexpr const char* RSYN_PRESENTATION_PIPELINE_ID = "synesthesia-rsyn-presentation-v12";

inline constexpr const char* RSYN_PREVIOUS_PRESENTATION_PIPELINE_ID = "synesthesia-rsyn-presentation-v11";
inline bool readablePresentationPipeline(const std::string& id) {
    return id == RSYN_PRESENTATION_PIPELINE_ID || id == RSYN_PREVIOUS_PRESENTATION_PIPELINE_ID ||
        id == "synesthesia-rsyn-presentation-v8" || id == "synesthesia-rsyn-presentation-v9" ||
        id == "synesthesia-rsyn-presentation-v10";
}

struct RSYNSmoothingSignals {
    bool onsetDetected = false;
    float spectralFlux = 0.0f;
    float spectralFlatness = 0.0f;
    float loudnessNormalised = 0.0f;
    float brightnessNormalised = 0.0f;
    float spectralSpreadNorm = 0.0f;
    float spectralRolloffNorm = 0.0f;
    float spectralCrestNorm = 0.0f;
    float phaseInstabilityNorm = 0.0f;
    float phaseCoherenceNorm = 0.0f;
    float phaseTransientNorm = 0.0f;
};

struct RSYNPresentationSettings {
    ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020;
    bool applyGamutMapping = true;
    float lowGain = 1.0f;
    float midGain = 1.0f;
    float highGain = 1.0f;
    bool smoothingEnabled = true;
    bool manualSmoothing = false;
    float smoothingAmount = 0.6f;
    std::string pipelineId = RSYN_PRESENTATION_PIPELINE_ID;
};

struct RSYNPresentationFrame {
    double timestamp = 0.0;
    ColourCore::FrameResult analysis{};
    RSYNSmoothingSignals smoothingSignals{};
    std::array<float, 3> targetOklab{};
    std::array<float, 3> smoothedOklab{};
    std::array<float, 3> smoothedLab{};
    std::array<float, 3> smoothedDisplayRgb{};
    float responseSeconds = 0.0f;
    float frequencyResolution = 0.0f;
    std::array<float, 3> presentedXYZ{};
    std::shared_ptr<const ColourPresentation::SpectralContributions> contributionTargets;
    std::shared_ptr<const ColourPresentation::SpectralContributions> presentedContributions;
};

struct RSYNPresentationData {
    RSYNPresentationSettings settings{};
    std::vector<RSYNPresentationFrame> frames;
};

struct RSYNSourceData {
    std::string filename;
    std::string extension;
    std::uint32_t crc32 = 0;
    std::vector<std::uint8_t> bytes;
};

struct RSYNLazyAsset {
    bool rebuildAnalysis = false;
    std::string filepath;
    RSYNContainer::ChunkIndex chunkIndex;
};

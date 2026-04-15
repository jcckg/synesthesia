#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "colour/colour_core.h"
#include "resyne/encoding/formats/rsyn_container.h"

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
    float smoothingUpdateFactor = 1.2f;
    float springMass = 0.3f;
    std::string pipelineId = "synesthesia-rsyn-presentation-v1";
};

struct RSYNPresentationFrame {
    double timestamp = 0.0;
    ColourCore::FrameResult analysis{};
    RSYNSmoothingSignals smoothingSignals{};
    std::array<float, 3> targetOklab{};
    std::array<float, 3> smoothedOklab{};
    std::array<float, 3> smoothedLab{};
    std::array<float, 3> smoothedDisplayRgb{};
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
    std::string filepath;
    RSYNContainer::ChunkIndex chunkIndex;
};

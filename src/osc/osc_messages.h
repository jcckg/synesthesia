#pragma once

#include "colour/colour_core.h"

#include <cstdint>
#include <optional>
#include <variant>

namespace Synesthesia::OSC {

struct OSCFrameMetaData {
    int32_t sampleRate = 0;
    int32_t fftSize = 0;
    int64_t frameTimestamp = 0;
};

struct OSCFrameSignalData {
    float dominantFrequencyHz = 0.0f;
    float dominantWavelengthNm = 0.0f;
    float visualiserMagnitude = 0.0f;
    float phaseRadians = 0.0f;
};

struct OSCFrameColourData {
    float displayR = 0.0f;
    float displayG = 0.0f;
    float displayB = 0.0f;
    float cieX = 0.0f;
    float cieY = 0.0f;
    float cieZ = 0.0f;
    float oklabL = 0.0f;
    float oklabA = 0.0f;
    float oklabB = 0.0f;
};

struct OSCFrameSpectralData {
    float flatness = 0.0f;
    float centroidHz = 0.0f;
    float spreadHz = 0.0f;
    float normalisedSpread = 0.0f;
    float rolloffHz = 0.0f;
    float crestFactor = 0.0f;
    float spectralFlux = 0.0f;
};

struct OSCFrameLoudnessData {
    float loudnessDb = 0.0f;
    float loudnessNormalised = 0.0f;
    float frameLoudnessDb = 0.0f;
    float momentaryLoudnessLUFS = 0.0f;
    float estimatedSPL = 0.0f;
    float luminanceCdM2 = 0.0f;
    float brightnessNormalised = 0.0f;
};

struct OSCFrameTransientData {
    float transientMix = 0.0f;
    bool onsetDetected = false;
};

struct OSCFramePhaseData {
    float instabilityNorm = 0.0f;
    float coherenceNorm = 0.0f;
    float transientNorm = 0.0f;
};

struct OSCSmoothingFeatureData {
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

struct OSCFrameData {
    OSCFrameMetaData meta;
    OSCFrameSignalData signal;
    OSCFrameColourData colour;
    OSCFrameSpectralData spectral;
    OSCFrameLoudnessData loudness;
    OSCFrameTransientData transient;
    OSCFramePhaseData phase;
    OSCSmoothingFeatureData smoothing;
};

struct OSCStats {
    uint64_t framesSent = 0;
    uint64_t messagesReceived = 0;
    uint32_t currentFps = 0;
    float averageSendTimeMs = 0.0f;
};

struct SetSmoothingEnabledCommand {
    bool enabled;
};

struct SetColourSmoothingSpeedCommand {
    float speed;
};

struct SetSpectrumSmoothingCommand {
    float amount;
};

struct SetColourSpaceCommand {
    ColourCore::ColourSpace colourSpace;
};

struct SetGamutMappingCommand {
    bool enabled;
};

using OSCCommand = std::variant<
    SetSmoothingEnabledCommand,
    SetColourSmoothingSpeedCommand,
    SetSpectrumSmoothingCommand,
    SetColourSpaceCommand,
    SetGamutMappingCommand
>;

struct PendingOSCSettings {
    std::optional<bool> smoothingEnabled;
    std::optional<float> colourSmoothingSpeed;
    std::optional<float> spectrumSmoothingAmount;
    std::optional<ColourCore::ColourSpace> colourSpace;
    std::optional<bool> gamutMappingEnabled;
};

}

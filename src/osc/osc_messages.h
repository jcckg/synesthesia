#pragma once

#include "colour/colour_core.h"

#include <cstdint>
#include <limits>
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
    float dominantMagnitude = std::numeric_limits<float>::quiet_NaN();
    float phaseRadians = std::numeric_limits<float>::quiet_NaN();
};

struct OSCFrameColourData {
    ColourCore::XYZ measurementXYZ{};
    bool measurementAvailable = false;
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
    float relativeSpreadNormalised = 0.0f;
    float rolloffNormalised = 0.0f;
    float crestNormalised = 0.0f;
    float rolloffHz = 0.0f;
    float crestFactor = 0.0f;
    float spectralFlux = 0.0f;
};

struct OSCFrameLoudnessData {
    float loudnessNormalised = 0.0f;
    float frameLoudnessDb = 0.0f;
    float momentaryLoudnessLUFS = ColourCore::LOUDNESS_DB_UNSPECIFIED;
    float brightnessNormalised = 0.0f;
};

struct OSCFrameTransientData {
    bool onsetDetected = false;
};

struct OSCFramePhaseData {
    float instabilityNorm = 0.0f;
    float coherenceNorm = 0.0f;
    float transientNorm = 0.0f;
};

struct OSCFrameData {
    OSCFrameMetaData meta;
    OSCFrameSignalData signal;
    OSCFrameColourData colour;
    OSCFrameSpectralData spectral;
    OSCFrameLoudnessData loudness;
    OSCFrameTransientData transient;
    OSCFramePhaseData phase;
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

struct SetColourSpaceCommand {
    ColourCore::ColourSpace colourSpace;
};

struct SetGamutMappingCommand {
    bool enabled;
};

using OSCCommand = std::variant<
    SetSmoothingEnabledCommand,
    SetColourSmoothingSpeedCommand,
    SetColourSpaceCommand,
    SetGamutMappingCommand
>;

struct PendingOSCSettings {
    std::optional<bool> smoothingEnabled;
    std::optional<float> colourSmoothingSpeed;
    std::optional<ColourCore::ColourSpace> colourSpace;
    std::optional<bool> gamutMappingEnabled;
};

}

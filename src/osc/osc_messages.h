#pragma once

#include "colour/colour_core.h"

#include <cstdint>
#include <optional>
#include <variant>

namespace Synesthesia::OSC {

struct OSCSpectralData {
    float flatness = 0.0f;
    float centroid = 0.0f;
    float spread = 0.0f;
    float normalisedSpread = 0.0f;
};

struct OSCFrameData {
    int32_t sampleRate = 0;
    int32_t fftSize = 0;
    int64_t frameTimestamp = 0;
    float frequency = 0.0f;
    float wavelength = 0.0f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float magnitude = 0.0f;
    float phase = 0.0f;
    OSCSpectralData spectral;
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

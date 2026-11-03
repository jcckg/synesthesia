#pragma once

#include "colour/colour_core.h"

#include <cstdint>
#include <array>
#include <limits>
#include <optional>
#include <memory>
#include <vector>
#include "colour/spectral_contributions.h"
#include <variant>

namespace Synesthesia::OSC {

inline constexpr float kBandLevelMinimumDb = -70.0f;

enum class OSCPlaybackStatus { Idle, Playing, Loading, Recording };

constexpr OSCPlaybackStatus playbackStatus(bool loading, bool recording, bool playing) {
    return loading ? OSCPlaybackStatus::Loading : recording ? OSCPlaybackStatus::Recording :
        playing ? OSCPlaybackStatus::Playing : OSCPlaybackStatus::Idle;
}

constexpr const char* statusName(OSCPlaybackStatus status) {
    switch (status) {
        case OSCPlaybackStatus::Playing: return "playing";
        case OSCPlaybackStatus::Loading: return "loading";
        case OSCPlaybackStatus::Recording: return "recording";
        default: return "idle";
    }
}

struct OSCFrameSmoothingData {
    bool enabled = true;
    bool manual = false;
    float amount = 0.6f;
    float responseSeconds = 0.0f;
    float minimumResponseSeconds = 0.0f;
    float maximumResponseSeconds = 0.0f;
};

constexpr const char* smoothingMode(const OSCFrameSmoothingData& smoothing) {
    return !smoothing.enabled ? "disabled" : smoothing.manual ? "manual" : "automatic";
}

struct OSCFrameColourAnalysis {
    bool available = false;
    float luminanceY = NAN;
    float chromaticityX = NAN;
    float chromaticityY = NAN;
    float lightness = NAN;
    float chroma = NAN;
    float hueDegrees = NAN;
    float colourChangePerSecond = NAN;
};

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
    ColourCore::RGB linear{};
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

struct OSCFrameBandData {
    bool available = false;
    std::array<float, ColourPresentation::contributionBandCount> smoothedMagnitudes{};
    std::array<float, ColourPresentation::contributionBandCount> spectralFlux{};
    std::array<float, ColourPresentation::contributionBandCount> onsetDetected{};
};

struct OSCFrameData {
    bool sourceAvailable = false;
    uint64_t frameId = 0;
    uint64_t sessionId = 0;
    uint64_t sourceId = 0;
    std::vector<float> magnitudes;
    std::vector<float> phases;
    std::vector<float> frequencies;
    std::shared_ptr<const ColourPresentation::SpectralContributions> sourceContributions;
    std::shared_ptr<const ColourPresentation::SpectralContributions> presentedContributions;
    OSCPlaybackStatus status = OSCPlaybackStatus::Idle;
    OSCFrameSmoothingData smoothing;
    OSCFrameColourAnalysis sourceAnalysis;
    OSCFrameColourAnalysis presentedAnalysis;
    OSCFrameMetaData meta;
    OSCFrameSignalData signal;
    OSCFrameColourData colour;
    OSCFrameColourData sourceColour;
    OSCFrameSpectralData spectral;
    OSCFrameLoudnessData loudness;
    OSCFrameTransientData transient;
    OSCFramePhaseData phase;
    OSCFrameBandData bands;
    ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020;
};

struct OSCStats {
    uint64_t framesSent = 0;
    uint64_t messagesReceived = 0;
    uint32_t currentFps = 0;
    float averageSendTimeMs = 0.0f;
    std::size_t bytesPerFrame = 0;
    std::size_t packetsPerFrame = 0;
    std::size_t largestPacket = 0;
    double bytesPerSecond = 0.0;
    std::uint64_t skippedFrames = 0, failedFrames = 0;
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

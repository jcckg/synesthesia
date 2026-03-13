#include "synesthesia_osc_integration.h"

#include "colour_mapper.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <type_traits>

namespace Synesthesia::OSC {

std::unique_ptr<SynesthesiaOSCIntegration> SynesthesiaOSCIntegration::instance_;
std::mutex SynesthesiaOSCIntegration::instanceMutex_;

namespace {

float interpolateMagnitude(const std::vector<float>& magnitudes, const float spectralCentroid, const float sampleRate) {
    if (magnitudes.size() < 2 || spectralCentroid <= 0.0f || sampleRate <= 0.0f) {
        return 0.0f;
    }

    const float fftSize = 2.0f * static_cast<float>(magnitudes.size() - 1);
    const float binFrequency = sampleRate / fftSize;
    const float floatingIndex = spectralCentroid / binFrequency;
    const size_t indexFloor = static_cast<size_t>(floatingIndex);
    const size_t indexCeil = indexFloor + 1;
    if (indexCeil >= magnitudes.size()) {
        return 0.0f;
    }

    const float fraction = floatingIndex - static_cast<float>(indexFloor);
    return (1.0f - fraction) * magnitudes[indexFloor] + fraction * magnitudes[indexCeil];
}

float interpolatePhase(const std::vector<float>& magnitudes, const std::vector<float>& phases, const float spectralCentroid, const float sampleRate) {
    if (magnitudes.size() < 2 || phases.size() != magnitudes.size() || spectralCentroid <= 0.0f || sampleRate <= 0.0f) {
        return 0.0f;
    }

    const float fftSize = 2.0f * static_cast<float>(magnitudes.size() - 1);
    const float binFrequency = sampleRate / fftSize;
    const float floatingIndex = spectralCentroid / binFrequency;
    const size_t indexFloor = static_cast<size_t>(floatingIndex);
    const size_t indexCeil = indexFloor + 1;
    if (indexCeil >= phases.size()) {
        return 0.0f;
    }

    const float fraction = floatingIndex - static_cast<float>(indexFloor);
    const float phaseA = phases[indexFloor];
    const float phaseB = phases[indexCeil];
    float phaseDifference = phaseB - phaseA;

    constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
    if (phaseDifference > std::numbers::pi_v<float>) {
        phaseDifference -= twoPi;
    } else if (phaseDifference < -std::numbers::pi_v<float>) {
        phaseDifference += twoPi;
    }

    const float interpolatedPhase = phaseA + fraction * phaseDifference;
    return std::fmod(interpolatedPhase + std::numbers::pi_v<float>, twoPi) - std::numbers::pi_v<float>;
}

int64_t currentTimestampMicros() {
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    return static_cast<int64_t>(std::max(now, static_cast<decltype(now)>(0)));
}

}

bool SynesthesiaOSCIntegration::start(const OSCConfig& config) {
    if (!runtime_.updateConfig(config)) {
        return false;
    }
    return runtime_.start();
}

void SynesthesiaOSCIntegration::stop() {
    runtime_.stop();
}

bool SynesthesiaOSCIntegration::isRunning() const {
    return runtime_.isRunning();
}

bool SynesthesiaOSCIntegration::updateConfig(const OSCConfig& config) {
    return runtime_.updateConfig(config);
}

OSCConfig SynesthesiaOSCIntegration::getConfig() const {
    return runtime_.getConfig();
}

std::string SynesthesiaOSCIntegration::getLastError() const {
    return runtime_.getLastError();
}

void SynesthesiaOSCIntegration::updateColourData(
    const std::vector<float>& magnitudes,
    const std::vector<float>& phases,
    const float spectralCentroid,
    const float sampleRate,
    const float r,
    const float g,
    const float b
) {
    if (!runtime_.isRunning()) {
        return;
    }

    const auto spectralCharacteristics = ColourMapper::calculateSpectralCharacteristics(magnitudes, sampleRate);

    OSCFrameData frame;
    frame.sampleRate = static_cast<int32_t>(sampleRate);
    frame.fftSize = magnitudes.size() > 1
        ? static_cast<int32_t>((magnitudes.size() - 1) * 2)
        : static_cast<int32_t>(magnitudes.size() * 2);
    frame.frameTimestamp = currentTimestampMicros();
    frame.frequency = spectralCentroid;
    frame.wavelength = ColourMapper::logFrequencyToWavelength(spectralCentroid);
    frame.r = r;
    frame.g = g;
    frame.b = b;
    frame.magnitude = interpolateMagnitude(magnitudes, spectralCentroid, sampleRate);
    frame.phase = interpolatePhase(magnitudes, phases, spectralCentroid, sampleRate);
    frame.spectral.flatness = spectralCharacteristics.flatness;
    frame.spectral.centroid = spectralCharacteristics.centroid;
    frame.spectral.spread = spectralCharacteristics.spread;
    frame.spectral.normalisedSpread = spectralCharacteristics.normalisedSpread;

    runtime_.sendFrame(frame);
}

PendingOSCSettings SynesthesiaOSCIntegration::consumePendingSettings() {
    PendingOSCSettings pendingSettings;
    const auto commands = runtime_.popPendingCommands();

    for (const OSCCommand& command : commands) {
        std::visit([&pendingSettings](const auto& value) {
            using ValueType = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<ValueType, SetSmoothingEnabledCommand>) {
                pendingSettings.smoothingEnabled = value.enabled;
            } else if constexpr (std::is_same_v<ValueType, SetColourSmoothingSpeedCommand>) {
                pendingSettings.colourSmoothingSpeed = value.speed;
            } else if constexpr (std::is_same_v<ValueType, SetSpectrumSmoothingCommand>) {
                pendingSettings.spectrumSmoothingAmount = value.amount;
            } else if constexpr (std::is_same_v<ValueType, SetColourSpaceCommand>) {
                pendingSettings.colourSpace = value.colourSpace;
            } else if constexpr (std::is_same_v<ValueType, SetGamutMappingCommand>) {
                pendingSettings.gamutMappingEnabled = value.enabled;
            }
        }, command);
    }

    return pendingSettings;
}

OSCStats SynesthesiaOSCIntegration::getStats() const {
    return runtime_.getStats();
}

SynesthesiaOSCIntegration& SynesthesiaOSCIntegration::getInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = std::unique_ptr<SynesthesiaOSCIntegration>(new SynesthesiaOSCIntegration());
    }
    return *instance_;
}

}

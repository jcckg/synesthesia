#include "synesthesia_osc_integration.h"

#include <type_traits>

namespace Synesthesia::OSC {

std::unique_ptr<SynesthesiaOSCIntegration> SynesthesiaOSCIntegration::instance_;
std::mutex SynesthesiaOSCIntegration::instanceMutex_;

bool SynesthesiaOSCIntegration::start(const OSCConfig& config) {
    std::lock_guard lock(frameMutex_);
    if (!runtime_.updateConfig(config)) {
        return false;
    }
    frameBuilder_.reset();
    return runtime_.start();
}

void SynesthesiaOSCIntegration::stop() {
    std::lock_guard lock(frameMutex_);
    runtime_.stop();
    frameBuilder_.reset();
}

bool SynesthesiaOSCIntegration::isRunning() const {
    return runtime_.isRunning();
}

bool SynesthesiaOSCIntegration::updateConfig(const OSCConfig& config) {
    std::lock_guard lock(frameMutex_);
    const bool updated = runtime_.updateConfig(config);
    if (updated) frameBuilder_.reset();
    return updated;
}

OSCConfig SynesthesiaOSCIntegration::getConfig() const {
    return runtime_.getConfig();
}

std::string SynesthesiaOSCIntegration::getLastError() const {
    return runtime_.getLastError();
}

void SynesthesiaOSCIntegration::updateFrameData(const OSCFrameUpdate& update) {
    std::lock_guard lock(frameMutex_);
    if (!runtime_.isRunning()) {
        return;
    }

    runtime_.sendFrame(frameBuilder_.build(update));
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

#pragma once

#include "osc_frame_builder.h"
#include "osc_config.h"
#include "osc_messages.h"
#include "osc_runtime.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Synesthesia::OSC {

class SynesthesiaOSCIntegration {
public:
    bool start(const OSCConfig& config = {});
    void stop();
    bool isRunning() const;

    bool updateConfig(const OSCConfig& config);
    OSCConfig getConfig() const;
    std::string getLastError() const;

    void updateFrameData(const OSCFrameUpdate& update);

    PendingOSCSettings consumePendingSettings();
    OSCStats getStats() const;

    static SynesthesiaOSCIntegration& getInstance();

private:
    SynesthesiaOSCIntegration() = default;

    OSCRuntime runtime_;

    static std::unique_ptr<SynesthesiaOSCIntegration> instance_;
    static std::mutex instanceMutex_;
};

}

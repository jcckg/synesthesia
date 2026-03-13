#pragma once

#include "osc_config.h"
#include "osc_messages.h"
#include "osc_runtime.h"

#include <memory>
#include <mutex>
#include <vector>

namespace Synesthesia::OSC {

class SynesthesiaOSCIntegration {
public:
    bool start(const OSCConfig& config = {});
    void stop();
    bool isRunning() const;

    void updateConfig(const OSCConfig& config);
    OSCConfig getConfig() const;

    void updateColourData(
        const std::vector<float>& magnitudes,
        const std::vector<float>& phases,
        float spectralCentroid,
        float sampleRate,
        float r,
        float g,
        float b
    );

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

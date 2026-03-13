#pragma once

#include "osc_command_queue.h"
#include "osc_config.h"
#include "osc_messages.h"
#include "osc_receiver.h"
#include "osc_sender.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace Synesthesia::OSC {

class OSCRuntime {
public:
    explicit OSCRuntime(const OSCConfig& config = {});

    bool start();
    void stop();
    bool isRunning() const;

    void updateConfig(const OSCConfig& config);
    OSCConfig getConfig() const;

    void sendFrame(const OSCFrameData& frame);
    std::vector<OSCCommand> popPendingCommands();
    OSCStats getStats() const;

private:
    void recordSendSample(float durationMs);

    mutable std::mutex mutex_;
    OSCConfig config_;
    OSCCommandQueue commandQueue_;
    OSCReceiver receiver_;
    OSCSender sender_;
    bool running_ = false;
    uint64_t framesSent_ = 0;
    uint32_t currentFps_ = 0;
    float averageSendTimeMs_ = 0.0f;
    uint32_t framesInWindow_ = 0;
    std::chrono::steady_clock::time_point fpsWindowStart_{};
};

}

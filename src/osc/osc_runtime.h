#pragma once

#include "osc_command_queue.h"
#include "osc_config.h"
#include "osc_messages.h"
#include "osc_receiver.h"
#include "osc_sender.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Synesthesia::OSC {

class OSCRuntime {
public:
    explicit OSCRuntime(const OSCConfig& config = {});

    ~OSCRuntime();
    bool start();
    void stop();
    bool isRunning() const;

    bool updateConfig(const OSCConfig& config);
    OSCConfig getConfig() const;
    void setRouting(const OSCRouting& routing, bool smoothBandMagnitudes);
    std::string getLastError() const;

    void sendFrame(std::shared_ptr<const OSCFrameData> frame);
    std::vector<OSCCommand> popPendingCommands();
    OSCStats getStats() const;

private:
    void recordSendSample(float durationMs);
    void transmit();
    std::condition_variable wake_;
    std::thread transmitThread_;
    std::deque<std::shared_ptr<const OSCFrameData>> pendingFrames_;
    std::uint64_t skippedFrames_ = 0, failedFrames_ = 0;

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
    std::string lastError_;
    OSCOutputSize lastSize_;
    std::size_t bytesInWindow_ = 0;
    double bytesPerSecond_ = 0.0;
};

}

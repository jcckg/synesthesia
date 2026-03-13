#include "osc_runtime.h"

namespace Synesthesia::OSC {

OSCRuntime::OSCRuntime(const OSCConfig& config)
    : config_(config), receiver_(commandQueue_) {}

bool OSCRuntime::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return true;
    }

    std::string errorMessage;
    if (!sender_.configure(config_, errorMessage)) {
        lastError_ = errorMessage;
        return false;
    }

    if (!receiver_.start(config_.receivePort)) {
        sender_.reset();
        lastError_ = "Unable to bind OSC receive socket on port " + std::to_string(config_.receivePort);
        return false;
    }

    running_ = true;
    fpsWindowStart_ = std::chrono::steady_clock::now();
    framesInWindow_ = 0;
    currentFps_ = 0;
    lastError_.clear();
    return true;
}

void OSCRuntime::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        sender_.reset();
        receiver_.stop();
        return;
    }

    receiver_.stop();
    sender_.reset();
    running_ = false;
    currentFps_ = 0;
    framesInWindow_ = 0;
    lastError_.clear();
}

bool OSCRuntime::isRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

bool OSCRuntime::updateConfig(const OSCConfig& config) {
    const auto destination = validateOSCDestination(config.destinationHost);
    if (!destination.valid) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = destination.errorMessage;
        return false;
    }

    OSCConfig normalisedConfig = config;
    normalisedConfig.destinationHost = destination.canonicalHost;

    const bool wasRunning = isRunning();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = normalisedConfig;
        lastError_.clear();
    }

    if (wasRunning) {
        stop();
        return start();
    }

    return true;
}

OSCConfig OSCRuntime::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

std::string OSCRuntime::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void OSCRuntime::sendFrame(const OSCFrameData& frame) {
    auto startTime = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
    }

    if (!sender_.sendFrame(frame)) {
        return;
    }

    const auto endTime = std::chrono::steady_clock::now();
    const float durationMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    recordSendSample(durationMs);
}

std::vector<OSCCommand> OSCRuntime::popPendingCommands() {
    return commandQueue_.popAll();
}

OSCStats OSCRuntime::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return OSCStats{
        framesSent_,
        receiver_.getReceivedMessageCount(),
        currentFps_,
        averageSendTimeMs_
    };
}

void OSCRuntime::recordSendSample(const float durationMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        return;
    }

    ++framesSent_;
    ++framesInWindow_;

    if (averageSendTimeMs_ == 0.0f) {
        averageSendTimeMs_ = durationMs;
    } else {
        averageSendTimeMs_ = averageSendTimeMs_ * 0.9f + durationMs * 0.1f;
    }

    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - fpsWindowStart_).count();
    if (elapsedSeconds >= 1.0f) {
        currentFps_ = static_cast<uint32_t>(static_cast<float>(framesInWindow_) / elapsedSeconds);
        fpsWindowStart_ = now;
        framesInWindow_ = 0;
    }
}

}

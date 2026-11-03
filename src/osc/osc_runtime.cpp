#include "osc_runtime.h"

namespace Synesthesia::OSC {

OSCRuntime::OSCRuntime(const OSCConfig& config)
    : config_(config), receiver_(commandQueue_) {}

OSCRuntime::~OSCRuntime() { stop(); }

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
    skippedFrames_ = 0;
    failedFrames_ = 0;
    pendingFrames_.clear();
    try { transmitThread_ = std::thread([this] { transmit(); }); }
    catch (const std::system_error& error) {
        running_ = false;
        receiver_.stop();
        sender_.reset();
        lastError_ = error.what();
        return false;
    }
    fpsWindowStart_ = std::chrono::steady_clock::now();
    framesInWindow_ = 0;
    bytesInWindow_ = 0;
    bytesPerSecond_ = 0.0;
    currentFps_ = 0;
    lastError_.clear();
    return true;
}

void OSCRuntime::stop() {
    {
        std::lock_guard lock(mutex_);
        running_ = false;
        pendingFrames_.clear();
        currentFps_ = 0;
        framesInWindow_ = 0;
    }
    wake_.notify_all();
    if (transmitThread_.joinable()) transmitThread_.join();
    receiver_.stop();
    sender_.reset();
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

void OSCRuntime::setRouting(const OSCRouting& routing, bool smoothBandMagnitudes) {
    std::lock_guard lock(mutex_);
    pendingFrames_.clear();
    sender_.setRouting(routing, smoothBandMagnitudes);
    config_.routing = routing;
    config_.smoothBandMagnitudes = smoothBandMagnitudes;
}

OSCConfig OSCRuntime::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

std::string OSCRuntime::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void OSCRuntime::sendFrame(std::shared_ptr<const OSCFrameData> frame) {
    {
        std::lock_guard lock(mutex_);
        if (!running_) return;
        if (pendingFrames_.size() >= 8) {
            pendingFrames_.pop_front();
            ++skippedFrames_;
        }
        pendingFrames_.push_back(std::move(frame));
    }
    wake_.notify_one();
}

void OSCRuntime::transmit() {
    for (;;) {
        std::shared_ptr<const OSCFrameData> frame;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return !running_ || !pendingFrames_.empty(); });
            if (!running_) return;
            frame = std::move(pendingFrames_.front());
            pendingFrames_.pop_front();
        }
        const auto start = std::chrono::steady_clock::now();
        if (sender_.sendFrame(*frame))
            recordSendSample(std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count());
        else {
            std::lock_guard lock(mutex_);
            ++failedFrames_;
            lastError_ = "Unable to send OSC frame";
        }
    }
}

std::vector<OSCCommand> OSCRuntime::popPendingCommands() {
    return commandQueue_.popAll();
}

OSCStats OSCRuntime::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool recent = running_ && std::chrono::steady_clock::now() - fpsWindowStart_ < std::chrono::seconds(2);
    return OSCStats{
        framesSent_,
        receiver_.getReceivedMessageCount(),
        recent ? currentFps_ : 0,
        averageSendTimeMs_,
        lastSize_.bytes,
        lastSize_.packets,
        lastSize_.largestPacket,
        recent ? bytesPerSecond_ : 0.0,
        skippedFrames_, failedFrames_
    };
}

void OSCRuntime::recordSendSample(const float durationMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        return;
    }

    lastSize_ = sender_.lastSize();
    if (lastSize_.packets == 0) return;
    bytesInWindow_ += lastSize_.bytes;
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
        bytesPerSecond_ = static_cast<double>(bytesInWindow_) / static_cast<double>(elapsedSeconds);
        bytesInWindow_ = 0;
        fpsWindowStart_ = now;
        framesInWindow_ = 0;
    }
}

}

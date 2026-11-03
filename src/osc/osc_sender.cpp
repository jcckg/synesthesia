#include "osc_sender.h"

#include "osc_routing.h"
#include <algorithm>

namespace Synesthesia::OSC {

OSCSender::~OSCSender() = default;

bool OSCSender::configure(const OSCConfig& config, std::string& errorMessage) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto destination = validateOSCDestination(config.destinationHost);
    if (!destination.valid) {
        socket_.reset();
        errorMessage = destination.errorMessage;
        return false;
    }

    try {
        config_ = config;
        config_.destinationHost = destination.canonicalHost;
        socket_ = std::make_unique<Utilities::DatagramSocket>(destination.address, config_.transmitPort);
        nextBandFrequencies_ = {};
        previousSession_ = previousSource_ = 0;
    } catch (const std::exception& exception) {
        socket_.reset();
        errorMessage = exception.what();
        return false;
    } catch (...) {
        socket_.reset();
        errorMessage = "Failed to create OSC transmit socket";
        return false;
    }

    errorMessage.clear();
    return true;
}

void OSCSender::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    socket_.reset();
    nextBandFrequencies_ = {};
    lastSize_ = {};
}

bool OSCSender::sendFrame(const OSCFrameData& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!socket_ || config_.outputBufferSize == 0) {
        return false;
    }

    try {
        const auto now = std::chrono::steady_clock::now();
        const bool frequenciesDue = now >= nextBandFrequencies_ || frame.sessionId != previousSession_ || frame.sourceId != previousSource_;
        const auto outputs = buildOutputs(frame, config_.routing, false, true, config_.smoothBandMagnitudes, frequenciesDue);
        const auto packets = encodePackets(frame, outputs, config_.routing, {config_.outputBufferSize, frequenciesDue});
        lastSize_ = {};
        for (const auto& packet : packets) {
            if (!socket_->send(packet.bytes.data(), packet.bytes.size())) return false;
            lastSize_.bytes += packet.bytes.size();
            ++lastSize_.packets;
            lastSize_.largestPacket = std::max(lastSize_.largestPacket, packet.bytes.size());
        }
        if (frequenciesDue) nextBandFrequencies_ = now + std::chrono::seconds(1);
        previousSession_ = frame.sessionId;
        previousSource_ = frame.sourceId;
    } catch (...) {
        return false;
    }

    return true;
}

void OSCSender::setRouting(const OSCRouting& routing, bool smoothBandMagnitudes) {
    std::lock_guard lock(mutex_);
    config_.routing = routing;
    config_.smoothBandMagnitudes = smoothBandMagnitudes;
    nextBandFrequencies_ = {};
}

OSCOutputSize OSCSender::lastSize() const {
    std::lock_guard lock(mutex_);
    return lastSize_;
}

}

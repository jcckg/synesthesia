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
        socket_ = std::make_unique<UdpTransmitSocket>(
            IpEndpointName(static_cast<unsigned long>(destination.address), static_cast<int>(config_.transmitPort))
        );
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
}

bool OSCSender::sendFrame(const OSCFrameData& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!socket_ || config_.outputBufferSize == 0) {
        return false;
    }

    try {
        const auto outputs = buildOutputs(frame, config_.routing, false);
        const auto packets = encodePackets(frame, outputs, config_.routing, config_.outputBufferSize);
        lastSize_ = {};
        for (const auto& packet : packets) {
            socket_->Send(packet.bytes.data(), packet.bytes.size());
            lastSize_.bytes += packet.bytes.size();
            ++lastSize_.packets;
            lastSize_.largestPacket = std::max(lastSize_.largestPacket, packet.bytes.size());
        }
    } catch (...) {
        return false;
    }

    return true;
}

void OSCSender::setRouting(const OSCRouting& routing) {
    std::lock_guard lock(mutex_);
    config_.routing = routing;
}

OSCOutputSize OSCSender::lastSize() const {
    std::lock_guard lock(mutex_);
    return lastSize_;
}

}

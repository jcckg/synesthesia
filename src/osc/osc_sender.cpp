#include "osc_sender.h"

#include "osc_addresses.h"

#include "ip/UdpSocket.h"
#include "osc/OscOutboundPacketStream.h"
#include "osc/OscTypes.h"

namespace Synesthesia::OSC {

OSCSender::~OSCSender() = default;

bool OSCSender::configure(const OSCConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        config_ = config;
        config_.destinationHost = kLoopbackHost;
        buffer_.assign(config.outputBufferSize, '\0');
        socket_ = std::make_unique<UdpTransmitSocket>(
            IpEndpointName(config_.destinationHost.c_str(), static_cast<int>(config_.transmitPort))
        );
    } catch (...) {
        socket_.reset();
        buffer_.clear();
        return false;
    }

    return true;
}

void OSCSender::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    socket_.reset();
    buffer_.clear();
}

bool OSCSender::sendFrame(const OSCFrameData& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!socket_ || buffer_.empty()) {
        return false;
    }

    try {
        osc::OutboundPacketStream packet(buffer_.data(), buffer_.size());
        packet << osc::BeginBundleImmediate
               << osc::BeginMessage(kFrameMetaAddress)
               << static_cast<osc::int32>(frame.sampleRate)
               << static_cast<osc::int32>(frame.fftSize)
               << static_cast<osc::int64>(frame.frameTimestamp)
               << osc::EndMessage
               << osc::BeginMessage(kFrameColourAddress)
               << frame.frequency
               << frame.wavelength
               << frame.r
               << frame.g
               << frame.b
               << frame.magnitude
               << frame.phase
               << osc::EndMessage
               << osc::BeginMessage(kFrameSpectralAddress)
               << frame.spectral.flatness
               << frame.spectral.centroid
               << frame.spectral.spread
               << frame.spectral.normalisedSpread
               << osc::EndMessage
               << osc::EndBundle;

        socket_->Send(packet.Data(), packet.Size());
    } catch (...) {
        return false;
    }

    return true;
}

}

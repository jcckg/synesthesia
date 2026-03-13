#include "osc_receiver.h"

#include "osc_packet_listener.h"

#include "ip/UdpSocket.h"

namespace Synesthesia::OSC {

OSCReceiver::OSCReceiver(OSCCommandQueue& commandQueue)
    : commandQueue_(commandQueue) {}

OSCReceiver::~OSCReceiver() {
    stop();
}

bool OSCReceiver::start(const uint16_t receivePort) {
    if (running_.load()) {
        return true;
    }

    try {
        listener_ = std::make_unique<OSCPacketListener>(commandQueue_, receivedMessages_);
        socket_ = std::make_unique<UdpListeningReceiveSocket>(
            IpEndpointName(IpEndpointName::ANY_ADDRESS, static_cast<int>(receivePort)),
            listener_.get()
        );
    } catch (...) {
        listener_.reset();
        socket_.reset();
        return false;
    }

    running_.store(true);
    thread_ = std::thread([this]() {
        if (socket_) {
            socket_->Run();
        }
        running_.store(false);
    });

    return true;
}

void OSCReceiver::stop() {
    if (!running_.load() && !thread_.joinable()) {
        listener_.reset();
        socket_.reset();
        return;
    }

    if (socket_) {
        socket_->AsynchronousBreak();
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    running_.store(false);
    socket_.reset();
    listener_.reset();
}

bool OSCReceiver::isRunning() const {
    return running_.load();
}

uint64_t OSCReceiver::getReceivedMessageCount() const {
    return receivedMessages_.load(std::memory_order_relaxed);
}

}

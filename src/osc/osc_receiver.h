#pragma once

#include "osc_command_queue.h"

#include <atomic>
#include <memory>
#include <thread>

class UdpListeningReceiveSocket;

namespace Synesthesia::OSC {

class OSCPacketListener;

class OSCReceiver {
public:
    explicit OSCReceiver(OSCCommandQueue& commandQueue);
    ~OSCReceiver();

    bool start(uint16_t receivePort);
    void stop();
    bool isRunning() const;
    uint64_t getReceivedMessageCount() const;

private:
    OSCCommandQueue& commandQueue_;
    std::unique_ptr<OSCPacketListener> listener_;
    std::unique_ptr<UdpListeningReceiveSocket> socket_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> receivedMessages_{0};
};

}

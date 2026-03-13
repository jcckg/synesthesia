#pragma once

#include "osc_command_queue.h"

#include "osc/OscPacketListener.h"

#include <atomic>

namespace Synesthesia::OSC {

class OSCPacketListener : public osc::OscPacketListener {
public:
    OSCPacketListener(OSCCommandQueue& commandQueue, std::atomic<uint64_t>& receivedMessages);

protected:
    void ProcessMessage(const osc::ReceivedMessage& message, const IpEndpointName& remoteEndpoint) override;

private:
    OSCCommandQueue& commandQueue_;
    std::atomic<uint64_t>& receivedMessages_;
};

}

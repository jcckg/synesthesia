#pragma once

#include "osc_config.h"
#include "osc_messages.h"

#include "ip/UdpSocket.h"

#include <memory>
#include <mutex>
#include <vector>

namespace Synesthesia::OSC {

class OSCSender {
public:
    OSCSender() = default;
    ~OSCSender();

    bool configure(const OSCConfig& config, std::string& errorMessage);
    void reset();
    bool sendFrame(const OSCFrameData& frame);

private:
    std::mutex mutex_;
    OSCConfig config_;
    std::unique_ptr<UdpTransmitSocket> socket_;
    std::vector<char> buffer_;
};

}

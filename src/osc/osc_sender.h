#pragma once

#include "osc_config.h"
#include "osc_messages.h"

#include "utilities/datagram_socket.h"

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
    OSCOutputSize lastSize() const;
    void setRouting(const OSCRouting& routing);

private:
    mutable std::mutex mutex_;
    OSCOutputSize lastSize_;
    OSCConfig config_;
    std::unique_ptr<Utilities::DatagramSocket> socket_;
};

}

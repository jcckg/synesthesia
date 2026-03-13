#pragma once

#include "osc_messages.h"

#include <mutex>
#include <vector>

namespace Synesthesia::OSC {

class OSCCommandQueue {
public:
    void push(OSCCommand command);
    std::vector<OSCCommand> popAll();

private:
    std::mutex mutex_;
    std::vector<OSCCommand> queue_;
};

}

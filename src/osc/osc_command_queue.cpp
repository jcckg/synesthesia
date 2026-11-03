#include "osc_command_queue.h"
#include <algorithm>

namespace Synesthesia::OSC {

void OSCCommandQueue::push(OSCCommand command) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(queue_, [&](const auto& pending) { return pending.index() == command.index(); });
    queue_.push_back(std::move(command));
}

std::vector<OSCCommand> OSCCommandQueue::popAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<OSCCommand> commands = std::move(queue_);
    queue_.clear();
    return commands;
}

}

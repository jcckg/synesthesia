#include "osc_command_queue.h"

namespace Synesthesia::OSC {

void OSCCommandQueue::push(OSCCommand command) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(command));
}

std::vector<OSCCommand> OSCCommandQueue::popAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<OSCCommand> commands = std::move(queue_);
    queue_.clear();
    return commands;
}

}

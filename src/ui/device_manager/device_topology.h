#pragma once

#include <memory>
#include <vector>
#include "audio/input/audio_input.h"
#include "audio/output/audio_output.h"

struct UIState;

namespace UI {
class DeviceTopology {
public:
    DeviceTopology(AudioInput& input, UIState& state, std::vector<AudioInput::DeviceInfo>& inputs,
        std::vector<AudioOutput::DeviceInfo>& outputs);
    ~DeviceTopology();
    void update();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

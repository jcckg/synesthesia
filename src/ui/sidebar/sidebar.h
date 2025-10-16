#pragma once

#include <imgui.h>
#include <vector>

#include "smoothing.h"
#include "ui.h"

namespace ReSyne {
    struct RecorderState;
}

namespace Sidebar {

struct LayoutMetrics {
    float width;
    float padding;
    float labelWidth;
    float controlWidth;
    float buttonHeight;
    float contentWidth;
};

struct RenderArgs {
    AudioInput& audioInput;
    const std::vector<AudioInput::DeviceInfo>& devices;
    const std::vector<AudioOutput::DeviceInfo>& outputDevices;
    float* clearColour;
    UIState& uiState;
    ReSyne::RecorderState& recorderState;
    SpringSmoother& colourSmoother;
    LayoutMetrics layout;
    ImVec2 displaySize;
    bool isPlaybackActive;
#ifdef ENABLE_MIDI
    MIDIInput* midiInput = nullptr;
    const std::vector<MIDIInput::DeviceInfo>* midiDevices = nullptr;
#endif
};

void render(RenderArgs& args);

}

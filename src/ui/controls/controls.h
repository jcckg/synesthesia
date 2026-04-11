#pragma once

#include "audio_input.h"
#include "smoothing.h"
#include "imgui.h"
#include <vector>

struct UIState;

#ifdef ENABLE_MIDI
#include "midi_input.h"
#endif

namespace ReSyne {
    struct RecorderState;
}

namespace Controls {
    void renderFrequencyInfoPanel(AudioInput& audioInput, float* clear_colour, const UIState& state, ReSyne::RecorderState& recorderState);
    
    void renderVisualiserSettingsPanel(SpringSmoother& colourSmoother, 
                                     float& smoothingAmount,
                                     bool& manualSmoothing,
                                     bool& showSpectrumAnalyser,
                                     float sidebarWidth,
                                     float sidebarPadding,
                                     float labelWidth,
                                     float controlWidth,
                                     float buttonHeight);

    void renderEQControlsPanel(float& lowGain,
                              float& midGain,
                              float& highGain,
                              bool enabled,
                              float sidebarWidth,
                              float sidebarPadding,
                              float labelWidth,
                              float controlWidth,
                              float buttonHeight,
                              float contentWidth);

    void renderAdvancedSettingsPanel(UIState& state
#ifdef ENABLE_MIDI
                                  , MIDIInput* midiInput = nullptr
                                  , const std::vector<MIDIInput::DeviceInfo>* midiDevices = nullptr
#endif
                                  );
}

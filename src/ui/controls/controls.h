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
                                     float sidebarWidth,
                                     float sidebarPadding,
                                     float labelWidth,
                                     float controlWidth,
                                     float buttonHeight);

    void renderEQControlsPanel(float& lowGain,
                              float& midGain,
                              float& highGain,
                              bool& showSpectrumAnalyser,
                              float& spectrumSmoothingFactor,
                              float sidebarWidth,
                              float sidebarPadding,
                              float labelWidth,
                              float controlWidth,
                              float buttonHeight,
                              float contentWidth);

    void renderAdvancedSettingsPanel(UIState& state, float contentWidth
#ifdef ENABLE_MIDI
                                      , MIDIInput* midiInput = nullptr
                                      , const std::vector<MIDIInput::DeviceInfo>* midiDevices = nullptr
#endif
                                      );
}
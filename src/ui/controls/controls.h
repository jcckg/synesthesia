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
    enum class SectionLayer { Primary, Secondary };

    bool renderSectionHeader(const char* label, ImGuiTreeNodeFlags flags = 0, SectionLayer layer = SectionLayer::Primary);
    void renderFrequencyInfoPanel(AudioInput& audioInput, ReSyne::RecorderState& recorderState);
    
    void renderColourSmoothingPanel(float& smoothingAmount,
                                     bool& manualSmoothing,
                                     bool& showSpectrumAnalyser,
                                     float sidebarWidth,
                                     float sidebarPadding,
                                     float buttonHeight,
                                     ReSyne::RecorderState* recorder = nullptr, float effectiveResponseSeconds = 0.0f, ColourPresentation::ResponseRange responseRange = {});

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

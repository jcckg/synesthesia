#pragma once

#include "audio_input.h"
#include "audio_output.h"
#include "update.h"
#include "styling.h"
#include "device_manager.h"
#include "resyne/controller/state.h"
#include "colour/colour_mapper.h"
#include "spectrum_analyser.h"
#include "imgui.h"
#include <vector>

#ifdef ENABLE_MIDI
#include "midi_input.h"
#include "midi_device_manager.h"
#endif

namespace UIConstants {
    static constexpr float DEFAULT_SAMPLE_RATE = 44100.0f;
    static constexpr float DEFAULT_SMOOTHING_SPEED = 0.6f;
    static constexpr float DEFAULT_GAMMA = 0.8f;
    static constexpr float COLOUR_SMOOTH_UPDATE_FACTOR = 1.2f;
    static constexpr float COLOUR_DECAY_RATE = 0.5f;
}

struct AudioSettings {
    float lowGain = 1.0f;
    float midGain = 1.0f;
    float highGain = 1.0f;
    std::vector<float> smoothedMagnitudes;
    float spectrumSmoothingFactor = 0.2f;
};

struct VisualSettings {
    float colourSmoothingSpeed = UIConstants::DEFAULT_SMOOTHING_SPEED;
    bool smoothingEnabled = true;
    ColourMapper::ColourSpace colourSpace = ColourMapper::ColourSpace::Rec2020;
    bool gamutMappingEnabled = true;

    enum class View {
        ReSyne,
        Visualisation
    };
    View activeView = View::ReSyne;
};

struct UIVisibility {
    bool showUI = true;
    bool showSpectrumAnalyser = true;
    bool showAdvancedSettings = false;
    bool showAPISettings = false;
    bool sidebarOnLeft = true;
};

struct UIState {
    AudioSettings audioSettings;
    VisualSettings visualSettings;
    UIVisibility visibility;

    DeviceState deviceState;

#ifdef ENABLE_MIDI
    MIDIDeviceState midiDeviceState;
    bool midiDevicesAvailable = false;
    bool midiAutoConnected = false;
    float lastMIDIDeviceCheckTime = 0.0f;
#endif

    SpectrumAnalyser spectrumAnalyser;
    StyleState styleState;
    UpdateState updateState;
    UpdateChecker updateChecker;

    bool apiServerEnabled = false;
    ReSyne::State resyneState;

    using View = VisualSettings::View;
};

void initialiseApp(UIState& state);

void updateUI(AudioInput &audioInput,
              const std::vector<AudioInput::DeviceInfo>& devices,
              const std::vector<AudioOutput::DeviceInfo>& outputDevices,
              float* clear_colour,
              ImGuiIO &io,
              UIState& state
#ifdef ENABLE_MIDI
              , MIDIInput* midiInput = nullptr,
              std::vector<MIDIInput::DeviceInfo>* midiDevices = nullptr
#endif
              );

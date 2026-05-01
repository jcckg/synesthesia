#pragma once

#include "audio_input.h"
#include "audio_output.h"
#include "update.h"
#include "styling.h"
#include "device_manager.h"
#include "resyne/controller/state.h"
#include "colour/colour_core.h"
#include "spectrum_analyser.h"
#include "imgui.h"
#include <array>
#include <string>
#include <vector>

#ifdef ENABLE_MIDI
#include "midi_input.h"
#include "midi_device_manager.h"
#endif

namespace Renderer {
class PresentationResources;
}

namespace UIConstants {
    static constexpr float DEFAULT_SAMPLE_RATE = 44100.0f;
    static constexpr float DEFAULT_SMOOTHING_SPEED = 0.6f;
    static constexpr float DEFAULT_SPECTRUM_SMOOTHING = 0.2f;
    static constexpr float COLOUR_SMOOTH_UPDATE_FACTOR = 1.2f;
    static constexpr float COLOUR_DECAY_RATE = 0.5f;
}

struct AudioSettings {
    float lowGain = 1.0f;
    float midGain = 1.0f;
    float highGain = 1.0f;
    std::vector<std::vector<float>> smoothedMagnitudes;
    float spectrumSmoothingFactor = UIConstants::DEFAULT_SPECTRUM_SMOOTHING;
};

struct VisualSettings {
    float colourSmoothingSpeed = UIConstants::DEFAULT_SMOOTHING_SPEED;
    bool smoothingEnabled = true;
    bool manualSmoothing = false;
    ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020;
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
    bool showOSCSettings = false;
    bool sidebarOnLeft = true;
};

struct OSCSettings {
    std::string destinationHost = "127.0.0.1";
    int transmitPort = 7000;
    int receivePort = 7001;
};

struct PresentationDiagnostics {
    enum class DisplaySurfacePrecision {
        Unknown,
        TenBit,
        EightBit
    };

    bool presentationResourcesAvailable = false;
    bool highPrecisionTexturesAvailable = false;
    bool backgroundPresentationAvailable = false;
    DisplaySurfacePrecision displaySurfacePrecision = DisplaySurfacePrecision::Unknown;
};

struct UIState {
    AudioSettings audioSettings;
    VisualSettings visualSettings;
    UIVisibility visibility;

    DeviceState deviceState;
    AudioInputLevelMonitor inputLevelMonitor;

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
    OSCSettings oscSettings;
    PresentationDiagnostics presentationDiagnostics;

    bool oscEnabled = false;
    ReSyne::State resyneState;
    Renderer::PresentationResources* presentationResources = nullptr;

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

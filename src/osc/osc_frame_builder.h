#pragma once

#include "audio/processing/audio_processor.h"
#include "colour/colour_core.h"
#include "osc_messages.h"
#include "ui/smoothing/smoothing.h"

#include <span>

namespace Synesthesia::OSC {

struct OSCAnalysisSignals {
    float momentaryLoudnessLUFS = -200.0f;
    float spectralFlux = 0.0f;
    bool onsetDetected = false;
};

using OSCSmoothingSignals = OSCSmoothingFeatureData;

struct OSCFrameUpdate {
    std::span<const float> magnitudes{};
    std::span<const float> phases{};
    float sampleRate = 0.0f;
    ColourCore::FrameResult colourResult{};
    ColourCore::RGB displayColour{};
    OSCAnalysisSignals analysisSignals{};
    OSCSmoothingSignals smoothingSignals{};
};

OSCAnalysisSignals buildAnalysisSignals(const AudioProcessor::SpectralData& spectralData);
OSCAnalysisSignals buildAnalysisSignals(float momentaryLoudnessLUFS,
                                        float spectralFlux,
                                        bool onsetDetected);
OSCSmoothingSignals buildSmoothingSignals(const SmoothingSignalFeatures& features);
OSCFrameData buildFrameData(const OSCFrameUpdate& update);

}

#pragma once

#include "colour/colour_core.h"
#include "osc_messages.h"

#include <span>
#include <cmath>
#include <limits>

namespace Synesthesia::OSC {

struct OSCAnalysisSignals {
    float spectralFlux = 0.0f;
    bool onsetDetected = false;
};

struct OSCFrameUpdate {
    std::span<const float> magnitudes{};
    std::span<const float> phases{};
    std::span<const float> frequencies{};
    int32_t fftSize = 0;
    float sampleRate = 0.0f;
    ColourCore::FrameResult colourResult{};
    ColourCore::RGB displayColour{};
    ColourCore::XYZ presentedXYZ{NAN, NAN, NAN};
    double sourceTimestamp = std::numeric_limits<double>::quiet_NaN();
    OSCAnalysisSignals analysisSignals{};
};

OSCFrameData buildFrameData(const OSCFrameUpdate& update);

}

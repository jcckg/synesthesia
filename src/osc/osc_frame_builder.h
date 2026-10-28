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
    OSCPlaybackStatus status = OSCPlaybackStatus::Idle;
    OSCFrameSmoothingData smoothing;
    bool sourceAvailable = true;
    uint64_t sourceId = 0;
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

class OSCFrameBuilder {
public:
    OSCFrameData build(const OSCFrameUpdate& update);
    void reset();

private:
    ColourCore::XYZ previousSource_{NAN, NAN, NAN};
    ColourCore::XYZ previousPresented_{NAN, NAN, NAN};
    double previousTimestamp_ = NAN;
    uint64_t sourceId_ = 0;
    OSCPlaybackStatus status_ = OSCPlaybackStatus::Idle;
    OSCFrameSmoothingData smoothing_;
    int sampleRate_ = 0;
    int fftSize_ = 0;
};

}

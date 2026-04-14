#pragma once

#include "resyne/recorder/recorder.h"

namespace ReSyne::RecorderColourCache {

struct CacheSettings {
    ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020;
    bool gamutMapping = true;
    float lowGain = 1.0f;
    float midGain = 1.0f;
    float highGain = 1.0f;
    bool smoothingEnabled = true;
    bool manualSmoothing = false;
    float smoothingAmount = 0.6f;
};

CacheSettings currentSettings(const RecorderState& state);

SampleColourEntry computeSampleColour(const AudioColourSample& sample,
    const CacheSettings& settings,
    const AudioColourSample* previousSample = nullptr);

}

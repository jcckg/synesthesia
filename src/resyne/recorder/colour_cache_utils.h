#pragma once

#include "resyne/recorder/recorder.h"

namespace ReSyne::RecorderColourCache {

struct CacheSettings {
    float gamma = 0.8f;
    ColourMapper::ColourSpace colourSpace = ColourMapper::ColourSpace::Rec2020;
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
    const CacheSettings& settings);

void markSettingsIfChanged(RecorderState& state,
    const CacheSettings& settings);

void ensureCacheLocked(RecorderState& state);

void appendSampleLocked(RecorderState& state,
	const AudioColourSample& sample);

void rebuildCache(RecorderState& state);

}

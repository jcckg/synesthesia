#pragma once

#include "resyne/recorder/recorder.h"

namespace ReSyne::RecorderColourCache {

SampleColourEntry computeSampleColour(const AudioColourSample& sample,
	float gamma,
	ColourMapper::ColourSpace colourSpace,
	bool gamutMapping);

void markSettingsIfChanged(RecorderState& state,
	float gamma,
	ColourMapper::ColourSpace colourSpace,
	bool gamutMapping);

void ensureCacheLocked(RecorderState& state);

void appendSampleLocked(RecorderState& state,
	const AudioColourSample& sample);

void rebuildCache(RecorderState& state);

}

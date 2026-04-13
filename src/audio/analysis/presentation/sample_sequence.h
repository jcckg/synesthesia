#pragma once

#include "audio/analysis/presentation/spectral_presentation.h"
#include "resyne/encoding/formats/exporter.h"

namespace SpectralPresentation::SampleSequence {

constexpr float kFallbackDeltaTimeSeconds = 1.0f / 60.0f;

Frame buildFrame(const AudioColourSample& sample);

float resolveDeltaTimeSeconds(const AudioColourSample* previousSample,
                              const AudioColourSample& currentSample,
                              float fallbackDeltaTimeSeconds = kFallbackDeltaTimeSeconds);

PreparedFrame prepareSampleFrame(const AudioColourSample& sample,
                                 const Settings& settings,
                                 const AudioColourSample* previousSample = nullptr,
                                 float fallbackDeltaTimeSeconds = kFallbackDeltaTimeSeconds);

ColourCore::FrameResult buildSampleColourResult(const AudioColourSample& sample,
                                                const Settings& settings,
                                                const AudioColourSample* previousSample = nullptr,
                                                float fallbackDeltaTimeSeconds = kFallbackDeltaTimeSeconds);

}

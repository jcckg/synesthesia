#include "resyne/recorder/colour_cache_utils.h"

#include <algorithm>
#include <cmath>

#include "audio/analysis/presentation/sample_sequence.h"
#include "audio/analysis/presentation/spectral_presentation.h"
#include "colour/colour_core.h"

namespace ReSyne::RecorderColourCache {

namespace {

SpectralPresentation::Settings buildPresentationSettings(const CacheSettings& settings) {
    SpectralPresentation::Settings presentation{};
    presentation.lowGain = settings.lowGain;
    presentation.midGain = settings.midGain;
    presentation.highGain = settings.highGain;
    presentation.colourSpace = settings.colourSpace;
    presentation.applyGamutMapping = settings.gamutMapping;
    return presentation;
}

SampleColourEntry entryFromColourResult(const ColourCore::FrameResult& colourResult) {
    SampleColourEntry entry{};
    entry.rgb.x = std::clamp(colourResult.r, 0.0f, 1.0f);
    entry.rgb.y = std::clamp(colourResult.g, 0.0f, 1.0f);
    entry.rgb.z = std::clamp(colourResult.b, 0.0f, 1.0f);
    entry.rgb.w = 1.0f;
    entry.labL = colourResult.L;
    entry.labA = colourResult.a;
    entry.labB = colourResult.b_comp;
    return entry;
}

}

CacheSettings currentSettings(const RecorderState& state) {
    CacheSettings settings{};
    settings.colourSpace = state.importColourSpace;
    settings.gamutMapping = state.importGamutMapping;
    settings.lowGain = state.importLowGain;
    settings.midGain = state.importMidGain;
    settings.highGain = state.importHighGain;
    settings.smoothingEnabled = state.presentationSmoothingEnabled;
    settings.manualSmoothing = state.presentationManualSmoothing;
    settings.smoothingAmount = state.presentationSmoothingAmount;
    return settings;
}

SampleColourEntry computeSampleColour(const AudioColourSample& sample,
                                      const CacheSettings& settings,
                                      const AudioColourSample*) {
    const float loudnessOverride = std::isfinite(sample.loudnessLUFS)
        ? sample.loudnessLUFS
        : ColourCore::LOUDNESS_DB_UNSPECIFIED;
    const auto colourResult = SpectralPresentation::buildColourResult(
        SpectralPresentation::SampleSequence::buildFrame(sample),
        buildPresentationSettings(settings),
        loudnessOverride);
    return entryFromColourResult(colourResult);
}

}

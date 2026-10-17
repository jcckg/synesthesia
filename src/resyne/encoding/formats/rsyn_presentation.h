#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "resyne/encoding/formats/exporter.h"
#include "colour/colour_track.h"
#include "audio/analysis/presentation/spectral_presentation.h"

namespace RSYNPresentation {

class ReplayState {
public:
    explicit ReplayState(const RSYNPresentationSettings& settings);
    ~ReplayState();
    RSYNPresentationFrame analyse(const AudioColourSample& sample, SpectralPresentation::PreparedFrame* prepared = nullptr);
    RSYNPresentationFrame process(const AudioColourSample& sample, SpectralPresentation::PreparedFrame* prepared = nullptr);
    RSYNPresentationFrame processAnalysed(const RSYNPresentationFrame& frame);
    void seedPresentation(const RSYNPresentationFrame& frame);
    void setEqualiserSettings(float low, float mid, float high);
    void setOutputSettings(ColourCore::ColourSpace colourSpace, bool gamutMapping);
    void setSmoothingSettings(bool enabled, bool manual, float amount);
private:
    struct Impl;
    std::unique_ptr<Impl> state;
};

double durationSeconds(const AudioMetadata& metadata, double lastTimestamp = 0.0);
std::shared_ptr<const ColourPresentation::ColourTrack> colourTrack(const RSYNPresentationData& presentation,
                                                                  bool smoothed);

std::shared_ptr<RSYNPresentationData> buildPresentationData(
    const std::vector<AudioColourSample>& samples,
    const RSYNPresentationSettings& settings,
    const std::function<void(float)>& progress = {});

}

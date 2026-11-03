#pragma once

#include "resyne/encoding/formats/rsyn_presentation.h"

namespace SpectralPresentation {

struct LiveResult {
    RSYNPresentationFrame frame;
    PreparedFrame prepared;
    std::shared_ptr<const Frame> source;
    RSYNPresentationSettings settings;
    int selectedChannel = -1;
    std::uint64_t processedFrames = 0;
    double captureCentreTime = 0, analysisCompletedTime = 0;
};

class LivePresentation {
public:
    void setSettings(const RSYNPresentationSettings& settings);
    const LiveResult& process(const AudioColourSample& sample);
    void reset(bool preserveColour = false);
private:
    RSYNPresentationSettings settings_;
    std::unique_ptr<RSYNPresentation::ReplayState> replay_;
    LiveResult result_;
    float sampleRate_ = 0.0f;
    std::uint32_t channels_ = 0;
    bool ready_ = false;
    void restart();
};

}

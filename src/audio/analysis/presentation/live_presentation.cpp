#include "live_presentation.h"
#include <stdexcept>

namespace SpectralPresentation {

void LivePresentation::restart() {
    replay_ = std::make_unique<RSYNPresentation::ReplayState>(settings_);
    if (ready_) replay_->seedPresentation(result_.frame);
}

void LivePresentation::setSettings(const RSYNPresentationSettings& settings) {
    if (settings.pipelineId != RSYN_PRESENTATION_PIPELINE_ID) throw std::invalid_argument("Unsupported presentation pipeline.");
    const bool changed = settings_.lowGain != settings.lowGain || settings_.midGain != settings.midGain ||
        settings_.highGain != settings.highGain;
    settings_ = settings;
    if (changed) restart();
    else if (replay_) {
        replay_->setOutputSettings(settings_.colourSpace, settings_.applyGamutMapping);
        replay_->setSmoothingSettings(settings_.smoothingEnabled, settings_.manualSmoothing, settings_.smoothingAmount);
    }
}

const LiveResult& LivePresentation::process(const AudioColourSample& sample) {
    const bool sameFormat = sample.sampleRate == sampleRate_ && sample.channels == channels_;
    if (ready_ && sameFormat && sample.timestamp == result_.frame.timestamp) return result_;
    if (!replay_ || !sameFormat ||
        (ready_ && sample.timestamp < result_.frame.timestamp)) restart();
    sampleRate_ = sample.sampleRate;
    channels_ = sample.channels;
    result_.frame = replay_->process(sample, &result_.prepared);
    result_.settings = settings_;
    ++result_.processedFrames;
    ready_ = true;
    return result_;
}

void LivePresentation::reset(bool preserveColour) {
    replay_.reset();
    sampleRate_ = 0.0f;
    channels_ = 0;
    if (preserveColour && ready_) result_.processedFrames = 0;
    else { ready_ = false; result_ = {}; }
}

}

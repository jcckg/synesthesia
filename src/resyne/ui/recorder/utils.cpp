#include "resyne/ui/recorder/utils.h"

#include <algorithm>

namespace ReSyne::RecorderUI {

std::optional<float> computePlaybackNormalisedPosition(const RecorderState& state) {
    if (state.audioOutput && !state.reconstructedAudio.empty()) {
        // Use getTotalFrames() to match getPlaybackPosition() which returns frames
        size_t total = state.audioOutput->getTotalFrames();
        if (total > 0) {
            size_t position = state.audioOutput->getPlaybackPosition();
            float normalised = static_cast<float>(position) / static_cast<float>(total);
            return std::clamp(normalised, 0.0f, 1.0f);
        }
    }
    return std::nullopt;
}

void handleTimelineScrub(RecorderState& state,
                         const Timeline::RenderResult& result) {
    if (!result.scrubberMoved) {
        return;
    }

    state.timeline.scrubberNormalisedPosition = result.newScrubberPosition;

    if (!state.samples.empty()) {
        Recorder::seekPlayback(state, result.newScrubberPosition);
    }
}

}

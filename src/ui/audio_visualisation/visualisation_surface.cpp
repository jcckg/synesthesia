#include "ui/audio_visualisation/visualisation_surface.h"

#include <algorithm>
#include <cmath>

#include "audio/input/audio_input.h"
#include "ui.h"

namespace UI::AudioVisualisation {

namespace {

float resolveSpectrumSampleRate(const UIState& state,
                                const AudioInput& audioInput,
                                const bool hasPlaybackSession) {
    if (hasPlaybackSession) {
        const auto& recorderState = state.resyneState.recorderState;
        if (recorderState.metadata.sampleRate > 0.0f) {
            return recorderState.metadata.sampleRate;
        }
        if (recorderState.fallbackSampleRate > 0.0f) {
            return recorderState.fallbackSampleRate;
        }
    }

    const float liveSampleRate = audioInput.getSampleRate();
    return liveSampleRate > 0.0f ? liveSampleRate : UIConstants::DEFAULT_SAMPLE_RATE;
}

} // namespace

std::array<float, 4> currentVisualisationClearColour(const UIState& state) {
    const auto sanitise = [](const float value) {
        if (!std::isfinite(value)) {
            return 0.0f;
        }
        return std::clamp(value, 0.0f, 1.0f);
    };

    return {
        sanitise(state.resyneState.displayColour[0]),
        sanitise(state.resyneState.displayColour[1]),
        sanitise(state.resyneState.displayColour[2]),
        1.0f
    };
}

void renderSpectrumOverlay(UIState& state,
                           const AudioInput& audioInput,
                           const SurfaceLayout& layout,
                           const bool hasPlaybackSession) {
    const bool hasLiveInput = state.deviceState.selectedDeviceIndex >= 0 && !state.deviceState.streamError;
    if ((!hasLiveInput && !hasPlaybackSession) || !state.visibility.showSpectrumAnalyser) {
        return;
    }

    state.spectrumAnalyser.drawSpectrumWindow(
        state.audioSettings.smoothedMagnitudes,
        layout.displaySize,
        resolveSpectrumSampleRate(state, audioInput, hasPlaybackSession),
        layout.sidebarWidth,
        layout.sidebarOnLeft,
        layout.bottomPanelHeight
    );
}

} // namespace UI::AudioVisualisation

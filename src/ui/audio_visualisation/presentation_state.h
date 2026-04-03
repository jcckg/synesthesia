#pragma once

#include "ui.h"
#include "ui/smoothing/smoothing.h"

namespace UI::AudioVisualisation {

struct ColourUpdateContext {
    float deltaTime;
    bool smoothingEnabled;
    bool manualSmoothing;
    SpringSmoother& colourSmoother;
    float* clearColour;
    UIState::View activeView;
};

void syncRecorderPresentationSettings(UIState& state);

bool hasPlaybackSession(const ReSyne::RecorderState& recorderState);

void processPlaybackState(AudioInput& audioInput,
                          UIState& state,
                          ReSyne::RecorderState& recorderState,
                          float& currentDisplayR,
                          float& currentDisplayG,
                          float& currentDisplayB,
                          const ColourUpdateContext& ctx);

void processLiveAudioState(AudioInput& audioInput,
                           UIState& state,
                           ReSyne::RecorderState& recorderState,
                           float& currentDisplayR,
                           float& currentDisplayG,
                           float& currentDisplayB,
                           const ColourUpdateContext& ctx);

}

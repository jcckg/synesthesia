#pragma once

#include "audio/processing/audio_processor.h"
#include "audio/input/audio_input.h"
#include "resyne/controller/state.h"

namespace ReSyne {

void updateFromFFT(State& state,
                   AudioProcessor& audioProcessor,
                   float sampleRate,
                   float currentR,
                   float currentG,
                   float currentB);

void renderMainView(State& state,
                    AudioInput& audioInput,
                    float windowX,
                    float windowY,
                    float windowWidth,
                    float windowHeight);

void renderRecorderPanel(State& state,
                         AudioProcessor& audioProcessor,
                         float panelX,
                         float panelY,
                         float panelWidth,
                         float panelHeight);

void handleDialogs(State& state);

}

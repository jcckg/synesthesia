#pragma once

#include "audio/analysis/fft/fft_processor.h"
#include "audio/input/audio_input.h"
#include "resyne/controller/state.h"

namespace ReSyne {

void updateFromFFT(State& state,
                   FFTProcessor& fftProcessor,
                   float sampleRate,
                   float currentR,
                   float currentG,
                   float currentB);

void renderMainView(State& state,
                    AudioInput& audioInput,
                    FFTProcessor& fftProcessor,
                    float windowX,
                    float windowY,
                    float windowWidth,
                    float windowHeight);

void renderRecorderPanel(State& state,
                         FFTProcessor& fftProcessor,
                         float panelX,
                         float panelY,
                         float panelWidth,
                         float panelHeight);

void handleDialogs(State& state);

}

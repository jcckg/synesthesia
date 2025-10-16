#include "resyne/controller/controller.h"

#include "resyne/recorder/recorder.h"

namespace ReSyne {

void updateFromFFT(State& state,
                   FFTProcessor& fftProcessor,
                   float sampleRate,
                   float currentR,
                   float currentG,
                   float currentB) {
    state.displayColour[0] = currentR;
    state.displayColour[1] = currentG;
    state.displayColour[2] = currentB;

    if (sampleRate > 0.0f) {
        state.recorderState.fallbackSampleRate = sampleRate;
    }
    state.recorderState.fallbackFftSize = FFTProcessor::FFT_SIZE;
    state.recorderState.fallbackHopSize = FFTProcessor::HOP_SIZE;

    Recorder::updateFromFFTProcessor(
        state.recorderState,
        fftProcessor,
        currentR,
        currentG,
        currentB);
}

void renderMainView(State& state,
                    FFTProcessor& fftProcessor,
                    float windowX,
                    float windowY,
                    float windowWidth,
                    float windowHeight) {
    Recorder::drawFullWindow(
        state.recorderState,
        fftProcessor,
        windowX,
        windowY,
        windowWidth,
        windowHeight,
        state.displayColour[0],
        state.displayColour[1],
        state.displayColour[2]);
}

void renderRecorderPanel(State& state,
                         FFTProcessor& fftProcessor,
                         float panelX,
                         float panelY,
                         float panelWidth,
                         float panelHeight) {
    Recorder::drawBottomPanel(
        state.recorderState,
        fftProcessor,
        panelX,
        panelY,
        panelWidth,
        panelHeight);
}

void handleDialogs(State& state) {
    Recorder::handleFileDialog(state.recorderState);
    Recorder::handleLoadDialog(state.recorderState);
}

}

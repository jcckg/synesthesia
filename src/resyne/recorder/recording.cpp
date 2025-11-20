#include "resyne/recorder/recorder.h"

#include "audio/analysis/fft/fft_processor.h"
#include "audio/processing/audio_processor.h"
#include "constants.h"
#include "resyne/recorder/colour_cache_utils.h"

namespace ReSyne {

RecorderState::~RecorderState() {
    if (importThread.joinable()) {
        importThread.join();
    }
    if (exportThread.joinable()) {
        exportThread.join();
    }
}

void Recorder::updateFromFFTProcessor(RecorderState& state,
                                                AudioProcessor& audioProcessor,
                                                float r,
                                                float g,
                                                float b) {
    if (!state.isRecording) {
        return;
    }

    (void)r;
    (void)g;
    (void)b;

    const size_t numChannels = audioProcessor.getChannelCount();
    if (numChannels == 0) {
        return;
    }

    auto frames = audioProcessor.getFFTProcessor(0).getBufferedFrames();
    if (frames.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(state.samplesMutex);

    for (const auto& frame : frames) {
        if (state.samples.size() >= RecorderState::MAX_SAMPLES) {
            state.isRecording = false;
            return;
        }

        if (state.samples.empty()) {
            state.firstFrameCounter = frame.frameCounter;
            state.metadata.sampleRate = frame.sampleRate;
            state.metadata.channels = static_cast<uint32_t>(numChannels);
        }

        AudioColourSample sample;
        sample.magnitudes.resize(numChannels);
        sample.phases.resize(numChannels);
        sample.channels = static_cast<uint32_t>(numChannels);
        sample.sampleRate = frame.sampleRate;
        sample.loudnessLUFS = frame.loudnessLUFS;
        sample.splDb = frame.loudnessLUFS + synesthesia::constants::REFERENCE_SPL_AT_0_LUFS;

        for (size_t ch = 0; ch < numChannels; ++ch) {
            auto channelFrames = audioProcessor.getFFTProcessor(ch).getBufferedFrames();
            if (!channelFrames.empty() && frame.frameCounter < channelFrames.size()) {
                bool foundMatch = false;
                for (const auto& chFrame : channelFrames) {
                    if (chFrame.frameCounter == frame.frameCounter) {
                        sample.magnitudes[ch] = chFrame.magnitudes;
                        sample.phases[ch] = chFrame.phases;
                        foundMatch = true;
                        break;
                    }
                }
                if (!foundMatch) {
                    sample.magnitudes[ch] = channelFrames[0].magnitudes;
                    sample.phases[ch] = channelFrames[0].phases;
                }
            } else {
                sample.magnitudes[ch] = std::vector<float>();
                sample.phases[ch] = std::vector<float>();
            }
        }

        uint64_t relativeFrame = frame.frameCounter - state.firstFrameCounter;
        sample.timestamp = static_cast<double>(relativeFrame * static_cast<uint64_t>(state.metadata.hopSize)) /
                           static_cast<double>(state.metadata.sampleRate);

        state.samples.push_back(sample);
        RecorderColourCache::appendSampleLocked(state, state.samples.back());
    }
}

void Recorder::startRecording(RecorderState& state,
                                         FFTProcessor& fftProcessor,
                                         int fftSize,
                                         int hopSize) {
    if (state.audioOutput) {
        state.audioOutput->stop();
        state.audioOutput->clearAudioData();
    }

    fftProcessor.getBufferedFrames();

    std::lock_guard<std::mutex> lock(state.samplesMutex);
    state.isRecording = true;
    state.firstFrameCounter = 0;
    state.samples.clear();
    state.sampleColourCache.clear();
    state.colourCacheDirty = true;
    state.isPlaybackInitialised = false;
    state.reconstructedAudio.clear();
    state.timeline.scrubberNormalisedPosition = 0.0f;
    state.timeline.isScrubberDragging = false;
    state.timeline.hoverOverlayAlpha = 0.0f;
    state.timeline.zoomFactor = 1.0f;
    state.timeline.viewCenterNormalised = 0.5f;
    state.timeline.grabStartViewCenter = 0.5f;
    state.timeline.trackScrubber = false;
    state.timeline.isZoomGestureActive = false;
    state.timeline.isGrabGestureActive = false;

    state.metadata.version = "3.0.0";
    state.metadata.sampleRate = 0.0f;
    state.metadata.fftSize = fftSize;
    state.metadata.hopSize = hopSize;
    state.metadata.windowType = "hann";
    state.metadata.numBins = static_cast<size_t>(fftSize / 2 + 1);
}

void Recorder::stopRecording(RecorderState& state) {
    state.isRecording = false;
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        state.metadata.numFrames = state.samples.size();
    }

    reconstructAudio(state);
}

}

#include "resyne/recorder/recorder.h"

#include "audio/analysis/fft/fft_processor.h"
#include "audio/processing/audio_processor.h"
#include "constants.h"
#include "resyne/recorder/colour_cache_utils.h"

namespace ReSyne {

namespace {

void resetTimelineState(RecorderState& state) {
    state.timeline.scrubberNormalisedPosition = 0.0f;
    state.timeline.isScrubberDragging = false;
    state.timeline.hoverOverlayAlpha = 0.0f;
    state.timeline.gradientRegionValid = false;
    state.timeline.zoomFactor = 1.0f;
    state.timeline.viewCentreNormalised = 0.5f;
    state.timeline.grabStartViewCentre = 0.5f;
    state.timeline.trackScrubber = false;
    state.timeline.isZoomGestureActive = false;
    state.timeline.isGrabGestureActive = false;
}

}

RecorderState::~RecorderState() {
    if (importThread.joinable()) {
        importThread.join();
    }
    if (exportThread.joinable()) {
        exportThread.join();
    }
}

bool Recorder::hasLoadedAudio(RecorderState& state) {
    std::lock_guard<std::mutex> lock(state.samplesMutex);
    return !state.samples.empty() ||
           !state.previewSamples.empty() ||
           !state.reconstructedAudio.empty() ||
           (state.audioOutput && state.audioOutput->getTotalSamples() > 0);
}

void Recorder::clearLoadedAudio(RecorderState& state) {
    state.isRecording = false;
    if (state.audioOutput) {
        state.audioOutput->stop();
        state.audioOutput->clearAudioData();
    }

    std::lock_guard<std::mutex> lock(state.samplesMutex);
    state.samples.clear();
    state.previewSamples.clear();
    state.importedSamples.clear();
    state.importedMetadata = {};
    state.importErrorMessage.clear();
    state.sampleColourCache.clear();
    state.colourCacheDirty = true;
    state.reconstructedAudio.clear();
    state.metadata = {};
    state.firstFrameCounter = 0;
    state.fallbackSampleRate = 0.0f;
    state.fallbackFftSize = 0;
    state.fallbackHopSize = 0;
    state.isPlaybackInitialised = false;
    state.dropFlashAlpha = 0.0f;
    state.statusMessage.clear();
    state.statusMessageTimer = 0.0f;
    state.loadingOperationStatus.clear();
    state.previewReady.store(false, std::memory_order_release);
    resetTimelineState(state);
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

    std::vector<std::vector<FFTProcessor::FFTFrame>> channelFrames(numChannels);
    size_t frameCount = 0;
    for (size_t ch = 0; ch < numChannels; ++ch) {
        channelFrames[ch] = audioProcessor.getFFTProcessor(ch).getBufferedFrames();
        if (channelFrames[ch].empty()) {
            return;
        }
        frameCount = ch == 0 ? channelFrames[ch].size() : std::min(frameCount, channelFrames[ch].size());
    }

    if (frameCount == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(state.samplesMutex);

    for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const auto& frame = channelFrames[0][frameIndex];
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

        bool channelsAligned = true;
        for (size_t ch = 0; ch < numChannels; ++ch) {
            const auto& channelFrame = channelFrames[ch][frameIndex];
            if (channelFrame.frameCounter != frame.frameCounter) {
                channelsAligned = false;
                break;
	            }

	            sample.magnitudes[ch] = channelFrame.magnitudes;
	            sample.phases[ch] = channelFrame.phases;
	        }

        if (!channelsAligned) {
            continue;
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
    fftProcessor.getBufferedFrames();
    clearLoadedAudio(state);

    std::lock_guard<std::mutex> lock(state.samplesMutex);
    state.isRecording = true;
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

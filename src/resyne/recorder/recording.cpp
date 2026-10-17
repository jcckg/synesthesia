#include "resyne/recorder/recorder.h"

#include <algorithm>
#include <limits>
#include <cmath>

#include "audio/analysis/fft/fft_processor.h"
#include "audio/processing/audio_processor.h"
#include "constants.h"
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
    presentationWorker.reset();
    if (exportThread.joinable()) {
        exportThread.join();
    }
}

void setLoadingOperationStatus(RecorderState& state, std::string status) {
    std::lock_guard<std::mutex> lock(state.operationStatusMutex);
    state.loadingOperationStatus = std::move(status);
}

std::string getLoadingOperationStatus(RecorderState& state) {
    std::lock_guard<std::mutex> lock(state.operationStatusMutex);
    return state.loadingOperationStatus;
}

void setExportOperationStatus(RecorderState& state, std::string status) {
    std::lock_guard<std::mutex> lock(state.operationStatusMutex);
    state.exportOperationStatus = std::move(status);
}

std::string getExportOperationStatus(RecorderState& state) {
    std::lock_guard<std::mutex> lock(state.operationStatusMutex);
    return state.exportOperationStatus;
}

bool Recorder::hasLoadedAudio(RecorderState& state) {
    std::lock_guard<std::mutex> lock(state.samplesMutex);
    return !state.samples.empty() ||
           !state.previewSamples.empty() ||
           !state.playbackAudio.empty() ||
           state.metadata.presentationData != nullptr ||
           (state.audioOutput && state.audioOutput->getTotalSamples() > 0);
}

void Recorder::clearLoadedAudio(RecorderState& state) {
    if (state.recordingProcessor != nullptr) state.recordingProcessor->cancelRecordingAnalysis();
    state.recordingProcessor = nullptr;
    state.isRecording = false;
    state.recordingCountdownActive = false;
    state.recordingCountdownRemaining = 0.0f;
    state.showClearTimelineDialog = false;
    if (state.audioOutput) {
        state.audioOutput->stop();
        state.audioOutput->clearAudioData();
    }

    std::lock_guard<std::mutex> lock(state.samplesMutex);
    ++state.presentationSourceGeneration;
    ++state.presentationPreviewGeneration;
    state.presentationSourceRevision = 0;
    state.presentationPreviewRevision = 0;
    state.samples.clear();
    state.previewSamples.clear();
    state.importedSamples.clear();
    state.importedMetadata = {};
    state.importedPlaybackAudio.clear();
    state.previewMetadata = {};
    state.importErrorMessage.clear();
    state.playbackAudio.clear();
    state.metadata = {};
    state.firstFrameCounter = 0;
    state.fallbackSampleRate = 0.0f;
    state.fallbackFftSize = 0;
    state.fallbackHopSize = 0;
    state.isPlaybackInitialised = false;
    state.playbackOutputRefreshPending = false;
    state.playbackStartPending = false;
    state.playbackRouteReadyTime = 0.0;
    state.dropFlashAlpha = 0.0f;
    state.statusMessage.clear();
    state.statusMessageTimer = 0.0f;
    setLoadingOperationStatus(state, {});
    state.previewReady.store(false, std::memory_order_release);
    resetTimelineState(state);
}

namespace {
void appendRecordingFrames(RecorderState& state, AudioProcessor::BufferedFrames channelFrames) {
    if (channelFrames.empty() || channelFrames.front().empty()) {
        return;
    }

    const size_t numChannels = channelFrames.size();
    size_t frameCount = channelFrames.front().size();
    for (size_t ch = 1; ch < numChannels; ++ch) {
        frameCount = std::min(frameCount, channelFrames[ch].size());
    }

    if (frameCount == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(state.samplesMutex);

    for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const auto& frame = channelFrames[0][frameIndex];
        if (state.samples.size() >= RecorderState::MAX_RECORDING_SAMPLES) {
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
            sample.bass.resize(numChannels);
            sample.kWeightedMagnitudes.resize(numChannels);
	        sample.channels = static_cast<uint32_t>(numChannels);
	        sample.sampleRate = frame.sampleRate;
        sample.loudnessLUFS = frame.loudnessLUFS;
        sample.splDb = std::numeric_limits<float>::quiet_NaN();

        bool channelsAligned = true;
        for (size_t ch = 0; ch < numChannels; ++ch) {
            const auto& channelFrame = channelFrames[ch][frameIndex];
            if (channelFrame.frameCounter != frame.frameCounter) {
                channelsAligned = false;
                break;
	            }

	            sample.magnitudes[ch] = channelFrame.magnitudes;
	            sample.phases[ch] = channelFrame.phases;
                sample.bass[ch] = channelFrame.bass;
                sample.kWeightedMagnitudes[ch] = channelFrame.kWeightedMagnitudes;
	        }

        if (!channelsAligned) {
            continue;
        }

        std::vector<float> channelLoudness(numChannels);
        for (size_t ch = 0; ch < numChannels; ++ch) {
            channelLoudness[ch] = channelFrames[ch][frameIndex].loudnessLUFS;
        }
        sample.loudnessLUFS = LoudnessMeter::combineChannelLoudness(channelLoudness);
        sample.splDb = std::numeric_limits<float>::quiet_NaN();

        uint64_t relativeFrame = frame.frameCounter - state.firstFrameCounter;
        sample.timestamp = (static_cast<double>(relativeFrame) * state.metadata.hopSize
                            + state.metadata.fftSize * 0.5 - state.metadata.analysisLeadingSamples) /
                           static_cast<double>(state.metadata.sampleRate);

        state.samples.push_back(sample);
    }
}

}

void Recorder::updateFromFFTProcessor(RecorderState& state, AudioProcessor& audioProcessor,
                                      float r, float g, float b) {
    (void)r; (void)g; (void)b;
    if (state.isRecording) {
        appendRecordingFrames(state, audioProcessor.consumeRecordingFrames());
        bool limitReached;
        {
            std::lock_guard lock(state.samplesMutex);
            limitReached = state.samples.size() >= RecorderState::MAX_RECORDING_SAMPLES;
        }
        if (limitReached) stopRecording(state);
    }
}

void Recorder::startRecording(RecorderState& state,
                                         AudioProcessor& audioProcessor,
                                         int fftSize,
                                         int hopSize) {
    fftSize = FFTProcessor::FFT_SIZE;
    hopSize = std::clamp(hopSize, 1, fftSize / 2);
    audioProcessor.discardBufferedFrames();
    clearLoadedAudio(state);

    std::lock_guard<std::mutex> lock(state.samplesMutex);
    audioProcessor.beginRecordingAnalysis(hopSize);
    state.recordingProcessor = &audioProcessor;
    state.isRecording = true;
    state.metadata.analysisLeadingSamples = fftSize - hopSize;
    state.metadata.sampleRate = 0.0f;
    state.metadata.fftSize = fftSize;
    state.metadata.hopSize = hopSize;
    state.metadata.windowType = "hann";
    state.metadata.numBins = static_cast<size_t>(fftSize / 2 + 1);
}

void Recorder::stopRecording(RecorderState& state) {
    if (state.recordingProcessor != nullptr) {
        std::uint64_t sampleCount = 0;
        float rate = 0.0f;
        auto frames = state.recordingProcessor->finishRecordingAnalysis(sampleCount, rate);
        appendRecordingFrames(state, std::move(frames));
        if (state.samples.size() >= RecorderState::MAX_RECORDING_SAMPLES)
            sampleCount = std::min(sampleCount, static_cast<std::uint64_t>(std::max(0.0, std::round(
                state.samples.back().timestamp * state.metadata.sampleRate + state.metadata.fftSize * .5))));
        state.metadata.originalSampleCount = sampleCount;
        state.metadata.durationSeconds = rate > 0.0f ? sampleCount / static_cast<double>(rate) : 0.0;
        state.recordingProcessor = nullptr;
    }
    state.isRecording = false;
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        state.metadata.numFrames = state.samples.size();
    }

    reconstructAudio(state);
}

}

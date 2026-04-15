#include "resyne/recorder/recorder.h"
#include "resyne/recorder/embedded_source_utils.h"
#include "resyne/recorder/reconstruction_utils.h"
#include "imgui_internal.h"

#include <algorithm>
#include <mutex>

namespace ReSyne {

bool Recorder::ensureRsynSamplesLoaded(RecorderState& state) {
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        if (!state.samples.empty()) {
            return true;
        }
        if (state.metadata.lazyAsset == nullptr) {
            return false;
        }
    }

    AudioMetadata metadata;
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        metadata = state.metadata;
    }

    std::vector<AudioColourSample> loadedSamples;
    if (!SequenceExporter::hydrateRsynSamples(metadata, loadedSamples) || loadedSamples.empty()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        state.metadata = std::move(metadata);
        state.samples = std::move(loadedSamples);
        state.timelinePreviewCache.clear();
        state.timelinePreviewCacheDirty = true;
    }

    return true;
}

bool Recorder::ensurePlaybackAudioLoaded(RecorderState& state) {
    bool hasPlaybackAudio = false;
    bool hasLazyAsset = false;
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        hasPlaybackAudio = !state.playbackAudio.empty();
        hasLazyAsset = state.metadata.lazyAsset != nullptr;
        if (hasPlaybackAudio) {
            state.isPlaybackInitialised = false;
        }
    }

    if (hasPlaybackAudio) {
        return refreshPlaybackOutput(state);
    }

    if (!hasLazyAsset) {
        reconstructAudio(state);
        return state.isPlaybackInitialised && !state.playbackAudio.empty();
    }

    AudioMetadata metadata;
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        metadata = state.metadata;
    }

    if (SequenceExporter::hydrateRsynSource(metadata)) {
        std::vector<float> playbackAudio;
        std::string errorMessage;
        if (EmbeddedSourceUtils::decodeEmbeddedSourceAudio(metadata, playbackAudio, errorMessage) &&
            !playbackAudio.empty()) {
            {
                std::lock_guard<std::mutex> lock(state.samplesMutex);
                state.metadata = std::move(metadata);
                state.playbackAudio = std::move(playbackAudio);
                state.timelinePreviewCacheDirty = true;
            }

            ensureRsynSamplesLoaded(state);
            return refreshPlaybackOutput(state);
        }
    }

    reconstructAudio(state);
    return state.isPlaybackInitialised && !state.playbackAudio.empty();
}

bool Recorder::refreshPlaybackOutput(RecorderState& state) {
    if (state.playbackAudio.empty()) {
        state.isPlaybackInitialised = false;
        return false;
    }

    const uint32_t numChannels = state.metadata.channels > 0
        ? state.metadata.channels
        : (!state.samples.empty() ? state.samples.front().channels : 1);
    const float sampleRate = state.metadata.sampleRate > 0.0f
        ? state.metadata.sampleRate
        : state.fallbackSampleRate;

    if (sampleRate <= 0.0f) {
        state.isPlaybackInitialised = false;
        return false;
    }

    if (!state.audioOutput) {
        state.audioOutput = std::make_unique<AudioOutput>();
    }

    const int deviceIndex = state.outputDeviceIndex;
    if (!state.audioOutput->initOutputStream(sampleRate, static_cast<int>(numChannels), deviceIndex)) {
        state.isPlaybackInitialised = false;
        return false;
    }

    state.audioOutput->setAudioData(state.playbackAudio, numChannels);
    state.audioOutput->setLoopEnabled(state.loopEnabled);
    state.isPlaybackInitialised = true;
    return true;
}

void Recorder::reconstructAudio(RecorderState& state) {
    ensureRsynSamplesLoaded(state);

    std::vector<AudioColourSample> samples;
    AudioMetadata metadata;
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        if (state.samples.empty()) {
            return;
        }
        samples = state.samples;
        metadata = state.metadata;
    }

    std::vector<float> rebuiltPlaybackAudio;
    if (!RecorderReconstruction::buildPlaybackAudio(samples, metadata, rebuiltPlaybackAudio)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        state.playbackAudio = std::move(rebuiltPlaybackAudio);
    }

    refreshPlaybackOutput(state);
}

void Recorder::startPlayback(RecorderState& state) {
    ensureRsynSamplesLoaded(state);

    if (!state.isPlaybackInitialised || state.playbackAudio.empty()) {
        if (!ensurePlaybackAudioLoaded(state)) {
            return;
        }
    }

    if (!state.audioOutput) {
        return;
    }

    if (!state.playbackAudio.empty()) {
        size_t totalFrames = state.audioOutput->getTotalFrames();
        size_t currentPosition = state.audioOutput->getPlaybackPosition();
        if (totalFrames > 0 && currentPosition >= totalFrames - 1) {
            state.audioOutput->seek(0);
            state.timeline.scrubberNormalisedPosition = 0.0f;
        } else {
            if (totalFrames == 0) {
                const uint32_t numChannels = !state.samples.empty() ? state.samples.front().channels : 1;
                totalFrames = numChannels > 0 ? state.playbackAudio.size() / numChannels : state.playbackAudio.size();
            }
            if (totalFrames > 0) {
                size_t startFrame = static_cast<size_t>(
                    std::clamp(state.timeline.scrubberNormalisedPosition, 0.0f, 1.0f) *
                    static_cast<float>(totalFrames));
                startFrame = std::min(startFrame, totalFrames - 1);
                size_t currentPos = state.audioOutput->getPlaybackPosition();
                if (currentPos != startFrame) {
                    state.audioOutput->seek(startFrame);
                }
            }
        }
    }

    state.audioOutput->setLoopEnabled(state.loopEnabled);
    state.audioOutput->play();
}

void Recorder::pausePlayback(RecorderState& state) {
    if (state.audioOutput) {
        state.audioOutput->pause();
    }
}

void Recorder::stopPlayback(RecorderState& state) {
    if (state.audioOutput) {
        state.audioOutput->stop();
    }
}

void Recorder::seekPlayback(RecorderState& state, float normalisedPosition) {
    float clamped = std::clamp(normalisedPosition, 0.0f, 1.0f);
    state.timeline.scrubberNormalisedPosition = clamped;

    if (state.audioOutput && !state.playbackAudio.empty()) {
        size_t totalFrames = state.audioOutput->getTotalFrames();
        if (totalFrames == 0) {
            const uint32_t numChannels = !state.samples.empty() ? state.samples.front().channels : 1;
            totalFrames = numChannels > 0 ? state.playbackAudio.size() / numChannels : state.playbackAudio.size();
        }
        if (totalFrames > 0) {
            size_t framePosition = static_cast<size_t>(clamped * static_cast<float>(totalFrames));
            framePosition = std::min(framePosition, totalFrames - 1);
            state.audioOutput->seek(framePosition);
        }
    }
}

void Recorder::handleKeyboardShortcuts(RecorderState& state,
                                       bool windowFocused,
                                       bool hasPlaybackData) {
    if (!windowFocused) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }

    const auto pressed = [](ImGuiKey key) {
        return ImGui::IsKeyPressed(key, false);
    };

    if (pressed(ImGuiKey_1)) {
        state.toolState.activeTool = UI::Utilities::ToolType::Cursor;
    } else if (pressed(ImGuiKey_2)) {
        state.toolState.activeTool = UI::Utilities::ToolType::Zoom;
    } else if (pressed(ImGuiKey_3) && !state.timeline.trackScrubber) {
        state.toolState.activeTool = UI::Utilities::ToolType::Grab;
    }

    if (pressed(ImGuiKey_0)) {
        state.timeline.trackScrubber = !state.timeline.trackScrubber;
        if (state.timeline.trackScrubber) {
            state.timeline.viewCentreNormalised = state.timeline.scrubberNormalisedPosition;
            state.toolState.activeTool = UI::Utilities::ToolType::Cursor;
            state.timeline.isGrabGestureActive = false;
        }
    }

    if (hasPlaybackData && !state.isRecording && pressed(ImGuiKey_Space)) {
        ImGui::ClearActiveID();
        const bool isPlaying = state.audioOutput && state.audioOutput->isPlaying();
        if (isPlaying) {
            pausePlayback(state);
        } else {
            startPlayback(state);
        }
    }
}

}

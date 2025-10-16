#include "resyne/recorder/recorder.h"

#include <algorithm>
#include <utility>
#include "resyne/encoding/audio/wav_encoder.h"

namespace ReSyne {

void Recorder::reconstructAudio(RecorderState& state) {
    std::lock_guard<std::mutex> lock(state.samplesMutex);
    if (state.samples.empty()) {
        return;
    }

    std::vector<SpectralSample> spectralSamples;
    spectralSamples.reserve(state.samples.size());
    for (const auto& sample : state.samples) {
        SpectralSample spectral;
        spectral.magnitudes = sample.magnitudes;
        spectral.phases = sample.phases;
        spectral.timestamp = sample.timestamp;
        spectral.sampleRate = sample.sampleRate;
        spectralSamples.push_back(spectral);
    }

    auto result = WAVEncoder::reconstructFromSpectralData(
        spectralSamples,
        state.metadata.sampleRate,
        state.metadata.fftSize,
        state.metadata.hopSize
    );

    if (!result.success || result.audioSamples.empty()) {
        return;
    }

    state.reconstructedAudio = std::move(result.audioSamples);

    if (!state.audioOutput) {
        state.audioOutput = std::make_unique<AudioOutput>();
    }

    int deviceIndex = state.outputDeviceIndex;
    state.audioOutput->initOutputStream(result.sampleRate, deviceIndex);
    state.audioOutput->setAudioData(state.reconstructedAudio);

    state.isPlaybackInitialised = true;
}

void Recorder::startPlayback(RecorderState& state) {
    if (!state.isPlaybackInitialised || state.reconstructedAudio.empty()) {
        reconstructAudio(state);
        if (!state.isPlaybackInitialised || state.reconstructedAudio.empty()) {
            return;
        }
    }

    if (!state.audioOutput) {
        return;
    }

    if (!state.reconstructedAudio.empty()) {
        size_t totalSamples = state.audioOutput->getTotalSamples();
        size_t currentPosition = state.audioOutput->getPlaybackPosition();
        if (totalSamples > 0 && currentPosition >= totalSamples - 1) {
            state.audioOutput->seek(0);
            state.timeline.scrubberNormalisedPosition = 0.0f;
        } else {
            if (totalSamples == 0) {
                totalSamples = state.reconstructedAudio.size();
            }
            if (totalSamples > 0) {
                size_t startSample = static_cast<size_t>(
                    std::clamp(state.timeline.scrubberNormalisedPosition, 0.0f, 1.0f) *
                    static_cast<float>(totalSamples));
                startSample = std::min(startSample, totalSamples - 1);
                size_t currentPos = state.audioOutput->getPlaybackPosition();
                if (currentPos != startSample) {
                    state.audioOutput->seek(startSample);
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

    if (state.audioOutput && !state.reconstructedAudio.empty()) {
        size_t totalSamples = state.audioOutput->getTotalSamples();
        if (totalSamples == 0) {
            totalSamples = state.reconstructedAudio.size();
        }
        if (totalSamples > 0) {
            size_t samplePosition = static_cast<size_t>(clamped * static_cast<float>(totalSamples));
            samplePosition = std::min(samplePosition, totalSamples - 1);
            state.audioOutput->seek(samplePosition);
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
            state.timeline.viewCenterNormalised = state.timeline.scrubberNormalisedPosition;
            state.toolState.activeTool = UI::Utilities::ToolType::Cursor;
            state.timeline.isGrabGestureActive = false;
        }
    }

    if (hasPlaybackData && !state.isRecording && pressed(ImGuiKey_Space)) {
        const bool isPlaying = state.audioOutput && state.audioOutput->isPlaying();
        if (isPlaying) {
            pausePlayback(state);
        } else {
            startPlayback(state);
        }
    }
}

}

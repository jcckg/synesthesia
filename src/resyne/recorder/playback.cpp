#include "resyne/recorder/recorder.h"
#include "imgui_internal.h"

#include <algorithm>
#include <utility>
#include "resyne/encoding/audio/wav_encoder.h"

namespace ReSyne {

void Recorder::reconstructAudio(RecorderState& state) {
    std::lock_guard<std::mutex> lock(state.samplesMutex);
    if (state.samples.empty()) {
        return;
    }

    const uint32_t numChannels = !state.samples.empty() ? state.samples.front().channels : 1;

    std::vector<std::vector<float>> channelAudioData(numChannels);
    size_t maxLength = 0;

    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        std::vector<SpectralSample> spectralSamples;
        spectralSamples.reserve(state.samples.size());

        for (const auto& sample : state.samples) {
            SpectralSample spectral;
            spectral.magnitudes.clear();
            spectral.phases.clear();
            spectral.frequencies.clear();
            if (ch < sample.magnitudes.size()) {
                spectral.magnitudes.push_back(sample.magnitudes[ch]);
            } else {
                spectral.magnitudes.push_back(std::vector<float>());
            }
            if (ch < sample.phases.size()) {
                spectral.phases.push_back(sample.phases[ch]);
            } else {
                spectral.phases.push_back(std::vector<float>());
            }
            if (ch < sample.frequencies.size()) {
                spectral.frequencies.push_back(sample.frequencies[ch]);
            } else {
                spectral.frequencies.push_back(std::vector<float>());
            }
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

        channelAudioData[ch] = std::move(result.audioSamples);
        maxLength = std::max(maxLength, channelAudioData[ch].size());
    }

    state.reconstructedAudio.clear();
    state.reconstructedAudio.reserve(maxLength * numChannels);

    for (size_t i = 0; i < maxLength; ++i) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            if (i < channelAudioData[ch].size()) {
                state.reconstructedAudio.push_back(channelAudioData[ch][i]);
            } else {
                state.reconstructedAudio.push_back(0.0f);
            }
        }
    }

    if (!state.audioOutput) {
        state.audioOutput = std::make_unique<AudioOutput>();
    }

    int deviceIndex = state.outputDeviceIndex;
    state.audioOutput->initOutputStream(state.metadata.sampleRate, static_cast<int>(numChannels), deviceIndex);
    state.audioOutput->setAudioData(state.reconstructedAudio, numChannels);

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
        size_t totalFrames = state.audioOutput->getTotalFrames();
        size_t currentPosition = state.audioOutput->getPlaybackPosition();
        if (totalFrames > 0 && currentPosition >= totalFrames - 1) {
            state.audioOutput->seek(0);
            state.timeline.scrubberNormalisedPosition = 0.0f;
        } else {
            if (totalFrames == 0) {
                const uint32_t numChannels = !state.samples.empty() ? state.samples.front().channels : 1;
                totalFrames = numChannels > 0 ? state.reconstructedAudio.size() / numChannels : state.reconstructedAudio.size();
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

    if (state.audioOutput && !state.reconstructedAudio.empty()) {
        size_t totalFrames = state.audioOutput->getTotalFrames();
        if (totalFrames == 0) {
            const uint32_t numChannels = !state.samples.empty() ? state.samples.front().channels : 1;
            totalFrames = numChannels > 0 ? state.reconstructedAudio.size() / numChannels : state.reconstructedAudio.size();
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

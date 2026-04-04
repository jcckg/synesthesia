#include "resyne/recorder/recorder.h"
#include "imgui_internal.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include "audio/analysis/presentation/spectral_presentation.h"
#include "resyne/encoding/audio/wav_encoder.h"

namespace ReSyne {

namespace {

void applyPlaybackBuffer(RecorderState& state,
                         std::vector<float> buffer,
                         const bool preservePosition,
                         const bool audibleApplied) {
    const bool wasPlaying = state.audioOutput && state.audioOutput->isPlaying();
    const size_t playbackPosition = state.audioOutput ? state.audioOutput->getPlaybackPosition() : 0;

    state.playbackAudio = std::move(buffer);
    state.audiblePlaybackApplied = audibleApplied;

    if (!state.audioOutput || !state.isPlaybackInitialised) {
        Recorder::refreshPlaybackOutput(state);
        if (preservePosition && state.audioOutput && !state.playbackAudio.empty()) {
            const uint32_t numChannels = state.metadata.channels > 0 ? state.metadata.channels : 1;
            const size_t totalFrames = numChannels > 0 ? state.playbackAudio.size() / numChannels : state.playbackAudio.size();
            if (totalFrames > 0) {
                state.audioOutput->seek(std::min(playbackPosition, totalFrames - 1));
            }
        }
        return;
    }

    const uint32_t numChannels = state.metadata.channels > 0
        ? state.metadata.channels
        : (!state.samples.empty() ? state.samples.front().channels : 1);

    if (wasPlaying) {
        state.audioOutput->pause();
    }

    state.audioOutput->setAudioData(state.playbackAudio, numChannels);
    state.audioOutput->setLoopEnabled(state.loopEnabled);
    state.isPlaybackInitialised = true;

    if (preservePosition && !state.playbackAudio.empty()) {
        const size_t totalFrames = state.audioOutput->getTotalFrames();
        if (totalFrames > 0) {
            state.audioOutput->seek(std::min(playbackPosition, totalFrames - 1));
        }
    }

    if (wasPlaying) {
        state.audioOutput->play();
    }
}

std::vector<float> buildAudiblePlaybackBuffer(const std::vector<AudioColourSample>& samples,
                                              const AudioMetadata& metadata,
                                              const SpectralPresentation::Settings& settings) {
    std::vector<SpectralSample> spectralSamples;
    spectralSamples.reserve(samples.size());

    for (const auto& sample : samples) {
        SpectralSample spectral;
        spectral.magnitudes.reserve(sample.magnitudes.size());
        spectral.phases = sample.phases;
        spectral.frequencies = sample.frequencies;
        spectral.timestamp = sample.timestamp;
        spectral.sampleRate = sample.sampleRate;

        for (size_t channelIndex = 0; channelIndex < sample.magnitudes.size(); ++channelIndex) {
            SpectralPresentation::Frame frame{};
            frame.magnitudes = sample.magnitudes[channelIndex];
            if (channelIndex < sample.phases.size()) {
                frame.phases = sample.phases[channelIndex];
            }
            if (channelIndex < sample.frequencies.size()) {
                frame.frequencies = sample.frequencies[channelIndex];
            }
            frame.sampleRate = sample.sampleRate;
            spectral.magnitudes.push_back(SpectralPresentation::buildColourMagnitudes(frame, settings));
        }

        spectralSamples.push_back(std::move(spectral));
    }

    auto result = WAVEncoder::reconstructFromSpectralData(
        spectralSamples,
        metadata.sampleRate,
        metadata.fftSize,
        metadata.hopSize);

    if (!result.success) {
        return {};
    }

    return result.audioSamples;
}

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

    state.sourcePlaybackAudio.clear();
    state.sourcePlaybackAudio.reserve(maxLength * numChannels);

    for (size_t i = 0; i < maxLength; ++i) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            if (i < channelAudioData[ch].size()) {
                state.sourcePlaybackAudio.push_back(channelAudioData[ch][i]);
            } else {
                state.sourcePlaybackAudio.push_back(0.0f);
            }
        }
    }

    state.playbackAudio = state.sourcePlaybackAudio;
    refreshPlaybackOutput(state);
}

void Recorder::syncAudiblePlayback(RecorderState& state,
                                   const bool enabled,
                                   const float lowGain,
                                   const float midGain,
                                   const float highGain) {
    if (state.audioOutput == nullptr || state.samples.empty() || state.sourcePlaybackAudio.empty()) {
        state.audiblePlaybackEnabled = enabled;
        state.audiblePlaybackLowGain = lowGain;
        state.audiblePlaybackMidGain = midGain;
        state.audiblePlaybackHighGain = highGain;
        return;
    }

    if (state.audiblePlaybackReady.load(std::memory_order_acquire)) {
        if (state.audiblePlaybackThread.joinable()) {
            state.audiblePlaybackThread.join();
        }

        const uint64_t completedSerial = state.audiblePlaybackCompletedSerial.load(std::memory_order_acquire);
        const uint64_t requestedSerial = state.audiblePlaybackRequestedSerial.load(std::memory_order_acquire);
        if (completedSerial == requestedSerial) {
            if (state.audiblePlaybackEnabled && !state.audiblePlaybackBuffer.empty()) {
                applyPlaybackBuffer(state, state.audiblePlaybackBuffer, true, true);
            } else {
                applyPlaybackBuffer(state, state.sourcePlaybackAudio, true, false);
            }
        }
        state.audiblePlaybackReady.store(false, std::memory_order_release);
        state.audiblePlaybackRunning.store(false, std::memory_order_release);
    }

    const bool changed =
        state.audiblePlaybackEnabled != enabled ||
        state.audiblePlaybackLowGain != lowGain ||
        state.audiblePlaybackMidGain != midGain ||
        state.audiblePlaybackHighGain != highGain;

    state.audiblePlaybackEnabled = enabled;
    state.audiblePlaybackLowGain = lowGain;
    state.audiblePlaybackMidGain = midGain;
    state.audiblePlaybackHighGain = highGain;

    const bool neutralGains =
        std::abs(lowGain - 1.0f) < 1e-6f &&
        std::abs(midGain - 1.0f) < 1e-6f &&
        std::abs(highGain - 1.0f) < 1e-6f;

    if (!enabled || neutralGains) {
        if (changed && state.audiblePlaybackApplied) {
            applyPlaybackBuffer(state, state.sourcePlaybackAudio, true, false);
        }
        return;
    }

    if (!changed && state.audiblePlaybackApplied) {
        return;
    }

    if (state.audiblePlaybackRunning.load(std::memory_order_acquire)) {
        if (changed) {
            state.audiblePlaybackRequestedSerial.fetch_add(1, std::memory_order_release);
        }
        return;
    }

    if (state.audiblePlaybackThread.joinable()) {
        state.audiblePlaybackThread.join();
    }

    const uint64_t serial = state.audiblePlaybackRequestedSerial.fetch_add(1, std::memory_order_release) + 1;

    std::vector<AudioColourSample> samplesCopy;
    AudioMetadata metadataCopy;
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        samplesCopy = state.samples;
        metadataCopy = state.metadata;
    }

    state.audiblePlaybackRunning.store(true, std::memory_order_release);
    state.audiblePlaybackThread = std::thread(
        [&state, serial, lowGain, midGain, highGain, samples = std::move(samplesCopy), metadata = std::move(metadataCopy)]() mutable {
            SpectralPresentation::Settings settings{};
            settings.lowGain = lowGain;
            settings.midGain = midGain;
            settings.highGain = highGain;
            const auto rebuiltAudio = buildAudiblePlaybackBuffer(samples, metadata, settings);
            if (serial == state.audiblePlaybackRequestedSerial.load(std::memory_order_acquire) && !rebuiltAudio.empty()) {
                state.audiblePlaybackBuffer = rebuiltAudio;
                state.audiblePlaybackCompletedSerial.store(serial, std::memory_order_release);
                state.audiblePlaybackReady.store(true, std::memory_order_release);
            } else {
                state.audiblePlaybackCompletedSerial.store(serial, std::memory_order_release);
                state.audiblePlaybackReady.store(true, std::memory_order_release);
            }
        });
}

void Recorder::startPlayback(RecorderState& state) {
    if (!state.isPlaybackInitialised || state.playbackAudio.empty()) {
        reconstructAudio(state);
        if (!state.isPlaybackInitialised || state.playbackAudio.empty()) {
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

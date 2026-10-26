#include "resyne/ui/recorder/shared_components.h"

#include "imgui.h"
#include "audio/input/audio_input.h"
#include "resyne/recorder/presentation_worker.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace ReSyne::UI {

ImVec4 loadingStatusColour() {
    ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    constexpr double fadeDownSeconds = 1.3;
    constexpr double fadeUpSeconds = 0.3;
    const double time = std::fmod(ImGui::GetTime(), fadeDownSeconds + fadeUpSeconds);
    const double phase = time < fadeDownSeconds ? time / fadeDownSeconds
        : 1.0 + (time - fadeDownSeconds) / fadeUpSeconds;
    const float pulse = static_cast<float>(0.5 - 0.5 * std::cos(phase * std::numbers::pi));
    colour.w *= 1.0f - 0.32f * pulse;
    return colour;
}

ImVec4 activityStatusColour(RecorderState& state, const AudioInput& audioInput, const PresentationSnapshot* presentation) {
    const bool recording = state.importPhase == 0 && state.isRecording;
    const bool playing = state.importPhase == 0 && state.audioOutput && state.audioOutput->isPlaying();
    if (!recording && !playing) {
        state.statusIntensity = 0.0f;
        return state.importPhase != 0 ? loadingStatusColour() : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }

    std::optional<float> loudnessDb;
    if (recording) {
        if (const auto live = audioInput.getAudioProcessor().getPresentation()) loudnessDb = live->frame.analysis.frameLoudnessDb;
    } else if (presentation && presentation->analysis && !presentation->analysis->empty()) {
        const double position = state.audioOutput->getPresentationPosition();
        const double time = state.metadata.sampleRate > 0.0f ? position / static_cast<double>(state.metadata.sampleRate)
            : state.audioOutput->getTotalFrames() > 0 ? position / static_cast<double>(state.audioOutput->getTotalFrames()) * presentation->duration
            : static_cast<double>(state.timeline.scrubberNormalisedPosition) * presentation->duration;
        loudnessDb = (*presentation->analysis)[presentation->analysis->indexAt(time)].analysis.frameLoudnessDb;
    }
    if (!loudnessDb.has_value() || std::isnan(*loudnessDb)) {
        const auto levels = recording ? audioInput.getStereoLevels() : state.audioOutput->getStereoLevels();
        float amplitude = 0.0f;
        for (float level : levels) {
            if (std::isfinite(level)) amplitude = std::max(amplitude, std::clamp(level, 0.0f, 1.0f));
        }
        loudnessDb = amplitude > 0.0f ? 20.0f * std::log10(amplitude) : -70.0f;
    }
    const float target = std::isfinite(*loudnessDb) ? std::clamp((*loudnessDb + 42.0f) / 36.0f, 0.0f, 1.0f) : 0.0f;
    const float response = target > state.statusIntensity ? 0.035f : 0.12f;
    const float blend = 1.0f - std::exp(-std::max(ImGui::GetIO().DeltaTime, 0.0f) / response);
    state.statusIntensity += (target - state.statusIntensity) * blend;

    const bool dark = ImGui::GetStyleColorVec4(ImGuiCol_Text).x > 0.5f;
    const ImVec4 quiet = recording
        ? (dark ? ImVec4(0.79f, 0.31f, 0.33f, 1.0f) : ImVec4(0.24f, 0.01f, 0.02f, 1.0f))
        : (dark ? ImVec4(0.16f, 0.54f, 0.28f, 1.0f) : ImVec4(0.01f, 0.145f, 0.02f, 1.0f));
    const ImVec4 loud = recording
        ? (dark ? ImVec4(1.0f, 0.45f, 0.47f, 1.0f) : ImVec4(0.82f, 0.15f, 0.17f, 1.0f))
        : (dark ? ImVec4(0.43f, 0.92f, 0.59f, 1.0f) : ImVec4(0.04f, 0.51f, 0.17f, 1.0f));
    return ImVec4(std::lerp(quiet.x, loud.x, state.statusIntensity), std::lerp(quiet.y, loud.y, state.statusIntensity),
        std::lerp(quiet.z, loud.z, state.statusIntensity), 1.0f);
}

void renderStatusMessage(RecorderState& state) {
    if (state.statusMessageTimer > 0.0f && !state.statusMessage.empty()) {
        ImGui::Spacing();

        constexpr float FADE_DURATION = 1.0f;
        float alpha = 1.0f;
        if (state.statusMessageTimer < FADE_DURATION) {
            alpha = state.statusMessageTimer / FADE_DURATION;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, alpha * 0.9f));
        ImGui::TextWrapped("%s", state.statusMessage.c_str());
        ImGui::PopStyleColor();
    }
}

}

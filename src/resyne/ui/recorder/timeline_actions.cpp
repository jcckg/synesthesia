#include "resyne/ui/recorder/timeline_actions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"
#include "resyne/recorder/recorder.h"

namespace ReSyne::RecorderUI {

namespace {

constexpr float kRecordingCountdownSeconds = 3.0f;
constexpr const char* kClearTimelinePopup = "Clear Timeline?";

}

void requestTimelineClear(RecorderState& state) {
    state.showClearTimelineDialog = true;
    ImGui::OpenPopup(kClearTimelinePopup);
}

void drawClearTimelineDialog(RecorderState& state) {
    if (state.showClearTimelineDialog) {
        ImGui::OpenPopup(kClearTimelinePopup);
    }

    if (ImGui::BeginPopupModal(kClearTimelinePopup, &state.showClearTimelineDialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 320.0f);
        ImGui::TextWrapped("Are you sure you want to clear the timeline?");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        if (ImGui::Button("(Yes) Clear Timeline")) {
            Recorder::clearLoadedAudio(state);
            state.showClearTimelineDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("(No) Keep Timeline")) {
            state.showClearTimelineDialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void beginRecordingCountdown(RecorderState& state) {
    if (state.isRecording) {
        return;
    }

    state.recordingCountdownActive = true;
    state.recordingCountdownRemaining = kRecordingCountdownSeconds;
}

void cancelRecordingCountdown(RecorderState& state) {
    state.recordingCountdownActive = false;
    state.recordingCountdownRemaining = 0.0f;
}

bool updateRecordingCountdown(RecorderState& state, float deltaSeconds) {
    if (!state.recordingCountdownActive) {
        return false;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        cancelRecordingCountdown(state);
        return false;
    }

    state.recordingCountdownRemaining -= std::max(0.0f, deltaSeconds);
    if (state.recordingCountdownRemaining > 0.0f) {
        return false;
    }

    cancelRecordingCountdown(state);
    return true;
}

const char* recordingStartButtonLabel(const RecorderState& state) {
    if (!state.recordingCountdownActive) {
        return "Start";
    }

    static char label[16];
    const int count = std::clamp(static_cast<int>(std::ceil(state.recordingCountdownRemaining)), 1, 3);
    std::snprintf(label, sizeof(label), "%d", count);
    return label;
}

}

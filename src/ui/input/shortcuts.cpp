#include "ui/input/shortcuts.h"

#include "imgui.h"
#include "resyne/recorder/recorder.h"
#include "ui/menu/application_menu.h"
#include "ui/ui.h"

namespace UI::Input {

void handleApplicationShortcuts([[maybe_unused]] UIState& state) {
#ifndef __APPLE__
    const auto& io = ImGui::GetIO();
    if (state.nativeMenuTracking || !io.KeyCtrl || io.KeyAlt || io.KeySuper) return;
    if (io.KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) Menu::executeCommand(Menu::Command::Export, state);
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) Menu::executeCommand(Menu::Command::ToggleExplorer, state);
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) Menu::executeCommand(Menu::Command::SearchExplorer, state);
#endif
}

void handlePlaybackShortcut(ReSyne::RecorderState& state, bool windowFocused) {
    if (state.importPhase != 0 || state.isRecording || state.recordingCountdownActive ||
        ImGui::GetIO().WantTextInput ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ||
        !ReSyne::Recorder::hasLoadedAudio(state)) {
        return;
    }

    const bool pressed = ImGui::Shortcut(ImGuiKey_Space,
        ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused);
    if (windowFocused && pressed) {
        ReSyne::Recorder::togglePlayback(state);
    }
}

void handleTimelineShortcuts(ReSyne::RecorderState& state, bool timelineFocused) {
    const auto& io = ImGui::GetIO();
    if (!timelineFocused || io.WantTextInput || io.KeySuper || io.KeyCtrl || io.KeyAlt || io.KeyShift ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;

    using ReSyne::UI::Utilities::ToolType;
    const auto pressed = [](ImGuiKey key) {
        return ImGui::IsKeyPressed(key, false);
    };

    if (pressed(ImGuiKey_1)) {
        state.toolState.activeTool = ToolType::Cursor;
    } else if (pressed(ImGuiKey_2)) {
        state.toolState.activeTool = ToolType::Zoom;
    } else if (pressed(ImGuiKey_3) && !state.timeline.trackScrubber) {
        state.toolState.activeTool = ToolType::Grab;
    }

    if (pressed(ImGuiKey_4)) {
        state.timeline.trackScrubber = !state.timeline.trackScrubber;
        if (state.timeline.trackScrubber) {
            state.timeline.viewCentreNormalised = state.timeline.scrubberNormalisedPosition;
            state.toolState.activeTool = ToolType::Cursor;
            state.timeline.isGrabGestureActive = false;
        }
    }
}

}

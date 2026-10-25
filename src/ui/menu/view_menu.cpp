#include "ui/menu/view_menu.h"

#include <array>

#include "ui/ui.h"

namespace UI::Menu {

std::span<const Item> viewMenuItems() {
    static constexpr std::array items{
        Item{"ReSyne View", Command::ReSyneView, "1"},
        Item{"Visualiser View", Command::VisualiserView, "2"},
        Item{},
        Item{.title = "Detach Visualiser", .command = Command::DetachVisualiser,
             .shortcut = "2", .shiftShortcut = true, .shortcutTitle = "Detach / reattach visualiser"},
        Item{},
        Item{.title = "Show/Hide Interface", .command = Command::ToggleInterface,
             .shortcut = "h", .shiftShortcut = true},
        Item{.title = "Spectrum Analyser", .command = Command::ToggleSpectrumAnalyser,
             .shortcut = "s", .shiftShortcut = true},
        Item{.title = "Follow Scrubber", .command = Command::ToggleFollowScrubber,
             .shortcut = "f", .shiftShortcut = true},
        Item{},
        Item{.title = "Swap Sidebar Sides", .command = Command::SwapSidebarSides,
             .shortcut = "l", .shiftShortcut = true}
    };
    return items;
}

State queryViewCommand(Command command, UIState& state) {
    const auto& detached = state.resyneState.recorderState.detachedVisualisation;
    State result;
    switch (command) {
        case Command::ReSyneView:
            result.enabled = true;
            result.checked = state.visualSettings.activeView == UIState::View::ReSyne;
            break;
        case Command::VisualiserView:
            result.enabled = !detached.isOpen && !detached.openRequested && !detached.reattachRequested;
            result.checked = state.visualSettings.activeView == UIState::View::Visualisation;
            break;
        case Command::DetachVisualiser:
            result.enabled = (detached.isOpen || detached.available) && !detached.openRequested && !detached.reattachRequested;
            result.title = detached.isOpen ? "Reattach Visualiser" : "Detach Visualiser";
            break;
        case Command::ToggleInterface:
            result.enabled = state.visualSettings.activeView == UIState::View::Visualisation;
            result.title = state.visibility.showUI ? "Hide Interface" : "Show Interface";
            break;
        case Command::ToggleSpectrumAnalyser:
            result.enabled = true;
            result.checked = state.visibility.showSpectrumAnalyser;
            break;
        case Command::ToggleFollowScrubber:
            result.enabled = state.resyneState.recorderState.importPhase == 0 &&
                             !state.resyneState.recorderState.importRunning.load(std::memory_order_acquire) &&
                             ReSyne::Recorder::hasLoadedAudio(state.resyneState.recorderState);
            result.checked = state.resyneState.recorderState.timeline.trackScrubber;
            break;
        case Command::SwapSidebarSides:
            result.enabled = true;
            break;
        default:
            break;
    }
    return result;
}

bool executeViewCommand(Command command, UIState& state) {
    if (!queryViewCommand(command, state).enabled) return false;

    auto& recorder = state.resyneState.recorderState;
    switch (command) {
        case Command::ReSyneView:
            state.visualSettings.activeView = UIState::View::ReSyne;
            break;
        case Command::VisualiserView:
            state.visualSettings.activeView = UIState::View::Visualisation;
            break;
        case Command::DetachVisualiser:
            if (recorder.detachedVisualisation.isOpen) recorder.detachedVisualisation.reattachRequested = true;
            else recorder.detachedVisualisation.openRequested = true;
            state.visualSettings.activeView = UIState::View::ReSyne;
            break;
        case Command::ToggleInterface:
            state.visibility.showUI = !state.visibility.showUI;
            return true;
        case Command::ToggleSpectrumAnalyser:
            state.visibility.showSpectrumAnalyser = !state.visibility.showSpectrumAnalyser;
            return true;
        case Command::ToggleFollowScrubber:
            recorder.timeline.trackScrubber = !recorder.timeline.trackScrubber;
            if (recorder.timeline.trackScrubber) {
                recorder.timeline.viewCentreNormalised = recorder.timeline.scrubberNormalisedPosition;
                recorder.toolState.activeTool = ReSyne::UI::Utilities::ToolType::Cursor;
                recorder.timeline.isGrabGestureActive = false;
                recorder.timeline.isZoomGestureActive = false;
            }
            state.visualSettings.activeView = UIState::View::ReSyne;
            break;
        case Command::SwapSidebarSides:
            state.visibility.sidebarOnLeft = !state.visibility.sidebarOnLeft;
            return true;
        default:
            return false;
    }
    state.visibility.showUI = true;
    recorder.focusRequested = true;
    return true;
}

}

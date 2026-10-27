#include "ui/menu/application_menu.h"

#include <array>
#include <cctype>
#include <utility>

#include "imgui_internal.h"
#include "ui/menu/file_menu.h"
#include "ui/menu/help_menu.h"
#include "ui/menu/tools_menu.h"
#include "ui/menu/view_menu.h"
#include "ui/ui.h"

namespace UI::Menu {

namespace {

void collectShortcuts(std::span<const Item> items, std::vector<Shortcut>& shortcuts) {
    for (const auto& item : items) {
        if (item.command != Command::None && item.shortcut[0] != '\0') {
#ifdef __APPLE__
            std::string keys = "Cmd + ";
#else
            if (item.command != Command::ToggleExplorer && item.command != Command::SearchExplorer &&
                item.command != Command::Export) continue;
            std::string keys = "Ctrl + ";
#endif
            if (item.shiftShortcut) keys += "Shift + ";
            for (const char* key = item.shortcut; *key != '\0'; ++key) {
                keys += static_cast<char>(std::toupper(static_cast<unsigned char>(*key)));
            }
            shortcuts.push_back({item.command, std::move(keys),
                                 item.shortcutTitle != nullptr ? item.shortcutTitle : item.title});
        }
        collectShortcuts(item.children, shortcuts);
    }
}

}

std::span<const Item> applicationMenus() {
    static const std::array menus{
        Item{"File", Command::None, "", fileMenuItems()},
        Item{"View", Command::None, "", viewMenuItems()},
#ifdef ENABLE_OSC
        Item{"Tools", Command::None, "", toolsMenuItems()},
#endif
        Item{.title = "Help", .children = helpMenuItems(), .isHelpMenu = true},
    };
    return menus;
}

std::vector<Shortcut> menuShortcuts() {
    std::vector<Shortcut> shortcuts;
    collectShortcuts(applicationMenus(), shortcuts);
    return shortcuts;
}

State queryCommand(Command command, UIState& state) {
    State result;
    switch (command) {
        case Command::ReSyneView:
        case Command::VisualiserView:
        case Command::ToggleExplorer:
        case Command::SearchExplorer:
        case Command::DetachVisualiser:
        case Command::ToggleInterface:
        case Command::ToggleSpectrumAnalyser:
        case Command::ToggleFollowScrubber:
        case Command::SwapSidebarSides:
            result = queryViewCommand(command, state);
            break;
        case Command::ToggleOSC:
            result = queryToolsCommand(command, state);
            break;
        case Command::KeyboardShortcuts:
        case Command::ProjectPage:
            result = queryHelpCommand(command, state);
            break;
        default:
            result.enabled = canExecuteFileCommand(command, state);
            break;
    }

    const auto& recorder = state.resyneState.recorderState;
    if (state.helpState.showKeyboardShortcuts || recorder.showExportDialog || recorder.showClearTimelineDialog ||
        recorder.shouldOpenLoadDialog || recorder.shouldOpenSaveDialog ||
        (ImGui::GetCurrentContext() != nullptr && ImGui::GetTopMostPopupModal() != nullptr)) {
        result.enabled = false;
    }
    return result;
}

bool executeCommand(Command command, UIState& state) {
    if (!queryCommand(command, state).enabled) return false;

    switch (command) {
        case Command::ReSyneView:
        case Command::VisualiserView:
        case Command::ToggleExplorer:
        case Command::SearchExplorer:
        case Command::DetachVisualiser:
        case Command::ToggleInterface:
        case Command::ToggleSpectrumAnalyser:
        case Command::ToggleFollowScrubber:
        case Command::SwapSidebarSides:
            return executeViewCommand(command, state);
        case Command::ToggleOSC:
            return executeToolsCommand(command, state);
        case Command::KeyboardShortcuts:
        case Command::ProjectPage:
            return executeHelpCommand(command, state);
        default:
            return executeFileCommand(command, state);
    }
}

}

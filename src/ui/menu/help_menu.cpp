#include "ui/menu/help_menu.h"

#include <array>

#include "ui/help/help.h"
#include "ui/ui.h"

namespace UI::Menu {

std::span<const Item> helpMenuItems() {
    static constexpr std::array items{
        Item{"Keyboard Shortcuts", Command::KeyboardShortcuts},
        Item{"Project on GitHub", Command::ProjectPage}
    };
    return items;
}

State queryHelpCommand(Command command, UIState& state) {
    State result;
    switch (command) {
        case Command::KeyboardShortcuts:
            result.enabled = !state.helpState.showKeyboardShortcuts;
            break;
        case Command::ProjectPage:
            result.enabled = true;
            break;
        default:
            break;
    }
    return result;
}

bool executeHelpCommand(Command command, UIState& state) {
    if (!queryHelpCommand(command, state).enabled) return false;

    switch (command) {
        case Command::KeyboardShortcuts:
            state.helpState.showKeyboardShortcuts = true;
            return true;
        case Command::ProjectPage:
            return UI::Help::openProjectPage();
        default:
            return false;
    }
}

}

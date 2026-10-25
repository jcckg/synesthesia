#pragma once

#include "ui/menu/menu_bar.h"

#include <vector>

struct UIState;

namespace UI::Menu {

std::span<const Item> applicationMenus();

struct Shortcut {
    Command command;
    std::string keys;
    std::string action;
};

std::vector<Shortcut> menuShortcuts();
State queryCommand(Command command, UIState& state);
bool executeCommand(Command command, UIState& state);

}

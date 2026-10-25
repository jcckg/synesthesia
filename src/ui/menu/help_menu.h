#pragma once

#include "ui/menu/menu_bar.h"

struct UIState;

namespace UI::Menu {

std::span<const Item> helpMenuItems();
State queryHelpCommand(Command command, UIState& state);
bool executeHelpCommand(Command command, UIState& state);

}

#pragma once

#include "ui/menu/menu_bar.h"

struct UIState;

namespace UI::Menu {

std::span<const Item> viewMenuItems();
State queryViewCommand(Command command, UIState& state);
bool executeViewCommand(Command command, UIState& state);

}

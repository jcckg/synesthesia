#pragma once

#include "ui/menu/menu_bar.h"

struct UIState;

namespace UI::Menu {

std::span<const Item> toolsMenuItems();
State queryToolsCommand(Command command, UIState& state);
bool executeToolsCommand(Command command, UIState& state);

}

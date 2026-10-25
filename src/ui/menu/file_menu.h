#pragma once

#include "ui/menu/menu_bar.h"

struct UIState;

namespace UI::Menu {

std::span<const Item> fileMenuItems();
bool canExecuteFileCommand(Command command, UIState& state);
bool executeFileCommand(Command command, UIState& state);

}

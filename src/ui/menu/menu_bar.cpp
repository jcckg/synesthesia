#include "ui/menu/menu_bar.h"

#ifndef __APPLE__

namespace UI::Menu {

struct MenuBar::Impl {};

MenuBar::MenuBar(std::span<const Item>, Query, Execute) {}
MenuBar::~MenuBar() = default;

void MenuBar::dispatchPendingCommands() {}
void MenuBar::shutdown() {}

}

#endif

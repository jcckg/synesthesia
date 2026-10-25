#pragma once

namespace UI::Help {

struct State {
    bool showKeyboardShortcuts = false;
};

void drawKeyboardShortcuts(State& state);
bool openProjectPage();

}

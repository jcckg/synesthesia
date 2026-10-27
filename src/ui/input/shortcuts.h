#pragma once

struct UIState;

namespace ReSyne {
struct RecorderState;
}

namespace UI::Input {

void handleApplicationShortcuts(UIState& state);
void handlePlaybackShortcut(ReSyne::RecorderState& state, bool windowFocused);
void handleTimelineShortcuts(ReSyne::RecorderState& state, bool timelineFocused);

}

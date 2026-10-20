#pragma once

namespace ReSyne {
struct RecorderState;
}

namespace UI::Input {

void handlePlaybackShortcut(ReSyne::RecorderState& state, bool windowFocused);
void handleTimelineShortcuts(ReSyne::RecorderState& state, bool timelineFocused);

}

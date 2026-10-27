#pragma once

#include <cstdint>

struct GLFWwindow;

namespace TrackpadGestures {

struct GestureFrame {
    bool hasScroll = false;
    bool scrollMomentum = false;
    bool scrollGestureBegan = false;
    double scrollPixelsY = 0.0;
    double scrollStepsY = 0.0;
    bool hasPinch = false;
    double pinchDelta = 0.0;
    bool hasHorizontalPan = false;
    double panDeltaX = 0.0;
    bool pointerValid = false;
    double cursorX = 0.0;
    double cursorY = 0.0;
};

void attach(GLFWwindow* window);
void shutdown();
GestureFrame consume();

}

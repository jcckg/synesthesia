#include "ui/input/trackpad_gestures.h"

#ifndef __APPLE__

namespace TrackpadGestures {

void attach(GLFWwindow*, bool) {}

void detach(GLFWwindow*) {}

void shutdown() {}

GestureFrame consume(GLFWwindow*) {
    return GestureFrame{};
}

}

#endif


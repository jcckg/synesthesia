#include "ui/input/trackpad_gestures.h"

#ifndef __APPLE__

namespace TrackpadGestures {

void attach(GLFWwindow*) {}

void shutdown() {}

GestureFrame consume() {
    return GestureFrame{};
}

}

#endif


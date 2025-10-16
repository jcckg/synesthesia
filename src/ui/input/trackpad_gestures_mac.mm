#include "ui/input/trackpad_gestures.h"

#ifdef __APPLE__

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#import <AppKit/AppKit.h>

#include <cmath>
#include <mutex>

namespace {

struct SharedState {
    std::mutex mutex;
    GLFWwindow* window = nullptr;
    TrackpadGestures::GestureFrame pending;
    id localMonitor = nil;
};

SharedState& sharedState() {
    static SharedState state;
    return state;
}

void updatePointer(SharedState& state) {
    if (!state.window) {
        return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(state.window, &cursorX, &cursorY);
    state.pending.pointerValid = true;
    state.pending.cursorX = cursorX;
    state.pending.cursorY = cursorY;
}

NSEvent* handleEvent(NSEvent* event) {
    SharedState& state = sharedState();

    std::lock_guard<std::mutex> lock(state.mutex);

    if (!state.window) {
        return event;
    }

    NSWindow* targetWindow = glfwGetCocoaWindow(state.window);
    if (!targetWindow || [event window] != targetWindow) {
        return event;
    }

    const NSEventType type = [event type];
    if (type == NSEventTypeMagnify) {
        const double magnification = [event magnification];
        if (std::fabs(magnification) > 1e-5) {
            state.pending.hasPinch = true;
            state.pending.pinchDelta += magnification;
            updatePointer(state);
        }
    } else if (type == NSEventTypeScrollWheel) {
        const bool precise = [event hasPreciseScrollingDeltas];

        if (precise) {
            updatePointer(state);

            const double deltaX = [event scrollingDeltaX];
            if (std::fabs(deltaX) > 1e-5) {
                const NSEventPhase phase = [event phase];
                const NSEventPhase momentumPhase = [event momentumPhase];

                if (phase == NSEventPhaseBegan || phase == NSEventPhaseChanged ||
                    momentumPhase == NSEventPhaseChanged) {
                    state.pending.hasHorizontalPan = true;
                    state.pending.panDeltaX += deltaX;
                }
            }
        }
    }

    return event;
}

void installMonitor(SharedState& state) {
    if (state.localMonitor != nil) {
        return;
    }

    NSEventMask mask = NSEventMaskMagnify | NSEventMaskScrollWheel;
    state.localMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent* (NSEvent* event) {
        return handleEvent(event);
    }];
}

void removeMonitor(SharedState& state) {
    if (state.localMonitor != nil) {
        [NSEvent removeMonitor:state.localMonitor];
        state.localMonitor = nil;
    }
}

}

namespace TrackpadGestures {

void attach(GLFWwindow* window) {
    SharedState& state = sharedState();
    std::lock_guard<std::mutex> lock(state.mutex);

    state.window = window;
    state.pending = GestureFrame{};
    if (window != nullptr) {
        installMonitor(state);
    } else {
        removeMonitor(state);
    }
}

void shutdown() {
    SharedState& state = sharedState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.window = nullptr;
    removeMonitor(state);
    state.pending = GestureFrame{};
}

GestureFrame consume() {
    SharedState& state = sharedState();
    std::lock_guard<std::mutex> lock(state.mutex);
    GestureFrame frame = state.pending;
    state.pending = GestureFrame{};
    return frame;
}

}

#endif // __APPLE__

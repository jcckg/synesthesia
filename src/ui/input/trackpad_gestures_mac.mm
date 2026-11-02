#include "ui/input/trackpad_gestures.h"

#ifdef __APPLE__

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#import <AppKit/AppKit.h>

#include <cmath>
#include <mutex>
#include <unordered_map>

namespace {

struct SharedState {
    std::mutex mutex;
    GLFWwindow* window = nullptr;
    std::unordered_map<GLFWwindow*, TrackpadGestures::GestureFrame> pending;
    id localMonitor = nil;
};

SharedState& sharedState() {
    static SharedState state;
    return state;
}

void updatePointer(GLFWwindow* window, TrackpadGestures::GestureFrame& pending) {
    if (!window) {
        return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    pending.pointerValid = true;
    pending.cursorX = cursorX;
    pending.cursorY = cursorY;
}

NSEvent* handleEvent(NSEvent* event) {
    SharedState& state = sharedState();

    std::lock_guard<std::mutex> lock(state.mutex);

    auto target = state.pending.end();
    for (auto iterator = state.pending.begin(); iterator != state.pending.end(); ++iterator) {
        if (glfwGetCocoaWindow(iterator->first) == [event window]) {
            target = iterator;
            break;
        }
    }
    if (target == state.pending.end()) return event;
    auto& pending = target->second;

    const NSEventType type = [event type];
    if (type == NSEventTypeMagnify) {
        const double magnification = [event magnification];
        if (std::fabs(magnification) > 1e-5) {
            pending.hasPinch = true;
            pending.pinchDelta += magnification;
            updatePointer(target->first, pending);
        }
    } else if (type == NSEventTypeScrollWheel) {
        const bool precise = [event hasPreciseScrollingDeltas];
        pending.hasScroll = true;
        pending.scrollMomentum = [event momentumPhase] != NSEventPhaseNone;
        pending.scrollGestureBegan |= [event phase] == NSEventPhaseBegan;
        if (precise) pending.scrollPixelsY += [event scrollingDeltaY];
        else pending.scrollStepsY += [event scrollingDeltaY];

        if (precise) {
            updatePointer(target->first, pending);

            const double deltaX = [event scrollingDeltaX];
            if (std::fabs(deltaX) > 1e-5) {
                const NSEventPhase phase = [event phase];
                const NSEventPhase momentumPhase = [event momentumPhase];

                if (phase == NSEventPhaseBegan || phase == NSEventPhaseChanged ||
                    momentumPhase == NSEventPhaseChanged) {
                    pending.hasHorizontalPan = true;
                    pending.panDeltaX += deltaX;
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

void attach(GLFWwindow* window, bool primary) {
    SharedState& state = sharedState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (primary) {
        if (state.window && state.window != window) state.pending.erase(state.window);
        state.window = window;
    }
    if (window != nullptr) {
        state.pending[window] = GestureFrame{};
        installMonitor(state);
    } else if (state.pending.empty()) removeMonitor(state);
}

void detach(GLFWwindow* window) {
    SharedState& state = sharedState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.pending.erase(window);
    if (state.window == window) state.window = nullptr;
    if (state.pending.empty()) removeMonitor(state);
}

void shutdown() {
    SharedState& state = sharedState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.window = nullptr;
    removeMonitor(state);
    state.pending.clear();
}

GestureFrame consume(GLFWwindow* window) {
    SharedState& state = sharedState();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto target = state.pending.find(window ? window : state.window);
    if (target == state.pending.end()) return {};
    const auto frame = target->second;
    target->second = GestureFrame{};
    return frame;
}

}

#endif
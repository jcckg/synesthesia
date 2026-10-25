#include "ui/input/continuous_pan.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "resyne/ui/timeline/timeline.h"
#include "resyne/ui/timeline/timeline_viewport.h"

namespace UI::Input {

ContinuousPan::ContinuousPan(GLFWwindow* window) : window_(window) {}

ContinuousPan::~ContinuousPan() {
    release();
}

void ContinuousPan::synchronise(ReSyne::Timeline::TimelineState& timeline,
                                 const ReSyne::UI::Utilities::ToolState& tools, bool allowed, bool canBegin) {
    const auto pressed = [this](int key) { return glfwGetKey(window_, key) == GLFW_PRESS; };
    const bool optionDown = pressed(GLFW_KEY_LEFT_ALT) || pressed(GLFW_KEY_RIGHT_ALT);
    const bool grabSelected = tools.activeTool == ReSyne::UI::Utilities::ToolType::Grab;
    const bool commandDown = pressed(GLFW_KEY_LEFT_SUPER) || pressed(GLFW_KEY_RIGHT_SUPER) ||
                             pressed(GLFW_KEY_LEFT_CONTROL) || pressed(GLFW_KEY_RIGHT_CONTROL);
    const bool dragging = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    const bool canPan = allowed && focused && dragging && (optionDown || grabSelected) && !commandDown &&
                        !pressed(GLFW_KEY_ESCAPE) && timeline.isGrabGestureActive && !timeline.trackScrubber &&
                        timeline.zoomFactor > ReSyne::Timeline::Viewport::MIN_ZOOM_FACTOR + 1e-3f;
    if (!canPan) {
        if (active_) {
            timeline.isGrabGestureActive = false;
            release();
        }
        return;
    }
    if (active_ || !canBegin) return;

    previousCursorMode_ = glfwGetInputMode(window_, GLFW_CURSOR);
    if (previousCursorMode_ == GLFW_CURSOR_DISABLED) return;

    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window_, &x, &y);
    timeline.grabGestureStart = ImVec2(static_cast<float>(x), static_cast<float>(y));
    timeline.grabStartViewCentre = timeline.viewCentreNormalised;
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    active_ = true;
}

void ContinuousPan::release() {
    if (!active_) return;
    glfwSetInputMode(window_, GLFW_CURSOR, previousCursorMode_);
    active_ = false;
}

}

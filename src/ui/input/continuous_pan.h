#pragma once

struct GLFWwindow;

namespace ReSyne::Timeline {
struct TimelineState;
}

namespace ReSyne::UI::Utilities {
struct ToolState;
}

namespace UI::Input {

class ContinuousPan {
public:
    explicit ContinuousPan(GLFWwindow* window);
    ~ContinuousPan();

    ContinuousPan(const ContinuousPan&) = delete;
    ContinuousPan& operator=(const ContinuousPan&) = delete;

    void synchronise(ReSyne::Timeline::TimelineState& timeline, const ReSyne::UI::Utilities::ToolState& tools,
                     bool allowed, bool canBegin);
    void release();

private:
    GLFWwindow* window_;
    int previousCursorMode_ = 0;
    bool active_ = false;
};

}

#pragma once

#include <unordered_map>

#include "imgui.h"
#include "ui/input/scroll_motion.h"
#include "ui/input/trackpad_gestures.h"

namespace UI::Input {

class Scrolling {
public:
    void beginFrame(const TrackpadGestures::GestureFrame& gestures, bool enabled = true);
    void advanceRegion(ScrollMotion& motion, ImGuiID id, bool hovered, float rowHeight, float maximum, bool enabled = true);

private:
    struct WindowMotion {
        ScrollMotion motion;
        float appliedPosition = 0.0f;
    };

    bool receivesInput(ImGuiID id, bool hovered);

    std::unordered_map<ImGuiID, WindowMotion> windows_;
    TrackpadGestures::GestureFrame gestures_;
    ImGuiID momentumTarget_ = 0;
    uint64_t momentumGeneration_ = 0;
    bool enabled_ = true;
    bool claimed_ = false;
};

}

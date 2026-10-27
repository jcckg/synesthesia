#include "ui/input/scrolling.h"

#include <algorithm>
#include <cmath>

#include "imgui_internal.h"

namespace UI::Input {

namespace {

bool acceptsScrolling(ImGuiWindow* window) {
    return window != nullptr && window->WasActive && !window->Collapsed &&
        !(window->Flags & ImGuiWindowFlags_NoMouseInputs) && ImGui::IsWindowContentHoverable(window);
}

ImGuiWindow* hoveredScrollWindow() {
    for (auto* window = GImGui->HoveredWindow; acceptsScrolling(window); window = window->ParentWindow) {
        if (window->Flags & ImGuiWindowFlags_NoScrollWithMouse) return nullptr;
        if (window->ScrollMax.y > 0.0f) return window;
        if (!(window->Flags & ImGuiWindowFlags_ChildWindow)) break;
    }
    return nullptr;
}

void cancelDefaultWheelScroll() {
    auto& context = *GImGui;
    auto* window = context.WheelingWindow;
    const auto& io = context.IO;
    if (window == nullptr || context.WheelingWindowScrolledFrame != context.FrameCount ||
        io.MouseWheel == 0.0f || io.KeyCtrl || io.MouseWheelRequestAxisSwap) return;
    const float step = ImTrunc(std::min(5.0f * window->FontRefSize, window->InnerRect.GetHeight() * 0.67f));
    if (window->ScrollTarget.y == window->Scroll.y - io.MouseWheel * step) window->ScrollTarget.y = FLT_MAX;
}

}

bool Scrolling::receivesInput(ImGuiID id, bool hovered) {
    if (!enabled_ || claimed_ || ImGui::GetIO().KeyCtrl || ImGui::GetIO().MouseWheelRequestAxisSwap) return false;
    const bool momentum = gestures_.hasScroll && gestures_.scrollMomentum && !gestures_.scrollGestureBegan;
    const bool receives = momentum ? momentumTarget_ == id : hovered;
    if (!receives) return false;
    if (gestures_.hasScroll || ImGui::GetIO().MouseWheel != 0.0f) {
        claimed_ = true;
        if (!momentum) momentumTarget_ = id;
    }
    return true;
}

void Scrolling::advanceRegion(ScrollMotion& motion, ImGuiID id, bool hovered, float rowHeight, float maximum, bool enabled) {
    const auto& io = ImGui::GetIO();
    if (momentumTarget_ == id && (!enabled || motion.resetGeneration != momentumGeneration_)) momentumTarget_ = 0;
    const bool receives = enabled && receivesInput(id, hovered);
    if (receives && claimed_) momentumGeneration_ = motion.resetGeneration;
    const float pixels = receives && gestures_.hasScroll ? static_cast<float>(gestures_.scrollPixelsY) : 0.0f;
    const float steps = !receives ? 0.0f : gestures_.hasScroll ?
        static_cast<float>(gestures_.scrollStepsY) : io.MouseWheel * 3.0f;
    motion.advance(pixels, steps, rowHeight, maximum, io.DeltaTime);
}

void Scrolling::beginFrame(const TrackpadGestures::GestureFrame& gestures, bool enabled) {
    auto& context = *GImGui;
    const auto& io = context.IO;
    const ImGuiID owner = ImGui::GetKeyOwner(ImGuiKey_MouseWheelY);
    gestures_ = gestures;
    enabled_ = enabled && !(io.ConfigFlags & ImGuiConfigFlags_NoMouse) && !io.AppFocusLost &&
        (owner == ImGuiKeyOwner_NoOwner || ImGui::FindWindowByID(owner) != nullptr);
    claimed_ = false;
    if (!enabled_ || ImGui::IsMouseClicked(ImGuiMouseButton_Left) || (gestures.hasScroll && (!gestures.scrollMomentum || gestures.scrollGestureBegan)) ||
        (!gestures.hasScroll && io.MouseWheel != 0.0f)) momentumTarget_ = 0;
    cancelDefaultWheelScroll();

    auto* hovered = enabled_ ? hoveredScrollWindow() : nullptr;
    if (hovered != nullptr && (gestures.hasScroll || io.MouseWheel != 0.0f)) {
        auto [iterator, inserted] = windows_.try_emplace(hovered->ID);
        if (inserted) {
            iterator->second.motion.reset(hovered->Scroll.y);
            iterator->second.appliedPosition = hovered->Scroll.y;
        }
    }

    for (auto iterator = windows_.begin(); iterator != windows_.end();) {
        auto* window = ImGui::FindWindowByID(iterator->first);
        if (!enabled_ || !acceptsScrolling(window) || (window->Flags & ImGuiWindowFlags_NoScrollWithMouse)) {
            if (momentumTarget_ == iterator->first) momentumTarget_ = 0;
            iterator = windows_.erase(iterator);
            continue;
        }
        auto& state = iterator->second;
        auto& motion = state.motion;
        if (std::abs(window->Scroll.y - state.appliedPosition) > 0.1f || window->ScrollTarget.y != FLT_MAX ||
            ImGui::GetActiveID() == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y)) {
            motion.reset(window->Scroll.y);
        }
        if (window->ScrollTarget.y == FLT_MAX) {
            advanceRegion(motion, window->ID, window == hovered, window->FontRefSize + ScrollMotion::rowPadding,
                window->ScrollMax.y);
            ImGui::SetScrollY(window, motion.position);
            state.appliedPosition = std::round(motion.position);
        }
        ++iterator;
    }
}

}

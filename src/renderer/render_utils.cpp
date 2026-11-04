#include "renderer/render_utils.h"

#include <algorithm>
#include <cmath>

#include "imgui_impl_glfw.h"
#include "renderer/window.h"

namespace Renderer {

uint32_t packRgba8(const float red, const float green, const float blue, const float alpha) {
    const auto toByte = [](const float value) -> uint8_t {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
    };

    const uint32_t rr = static_cast<uint32_t>(toByte(red));
    const uint32_t gg = static_cast<uint32_t>(toByte(green));
    const uint32_t bb = static_cast<uint32_t>(toByte(blue));
    const uint32_t aa = static_cast<uint32_t>(toByte(alpha));
    return (rr << 24) | (gg << 16) | (bb << 8) | aa;
}

float uiDpiScale(const Window& window) {
    return uiDpiScale(window.handle());
}

float uiDpiScale(GLFWwindow* window) {
    if (window == nullptr) return 1.0f;
    const float scale = ImGui_ImplGlfw_GetContentScaleForWindow(window);
    return std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
}

}

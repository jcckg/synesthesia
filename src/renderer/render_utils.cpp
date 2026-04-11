#include "renderer/render_utils.h"

#include <algorithm>

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
    const float rawScale = std::max(1.0f, ImGui_ImplGlfw_GetContentScaleForWindow(window.handle()));
#if defined(_WIN32)
    return 1.0f + (rawScale - 1.0f) * 0.65f;
#else
    return rawScale;
#endif
}

} // namespace Renderer

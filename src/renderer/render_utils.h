#pragma once

#include <cstdint>

namespace Renderer {

class Window;

uint32_t packRgba8(float red, float green, float blue, float alpha);
float uiDpiScale(const Window& window);

} // namespace Renderer

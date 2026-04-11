#pragma once

#include <bgfx/bgfx.h>

namespace Renderer {

class Window;

class BgfxContext {
public:
    static constexpr bgfx::ViewId kClearViewId = 0;
    static constexpr bgfx::ViewId kImGuiViewId = 1;

    BgfxContext();

    bool initialise(const Window& window, uint32_t width, uint32_t height);
    void shutdown();
    void reset(uint32_t width, uint32_t height);
    void setViewRects(uint16_t width, uint16_t height) const;

    [[nodiscard]] bool isInitialised() const;
    [[nodiscard]] uint32_t resetFlags() const;
    [[nodiscard]] bgfx::RendererType::Enum rendererType() const;
    [[nodiscard]] bool supportsMultipleWindows() const;

private:
    uint32_t reset_flags_ = BGFX_RESET_VSYNC;
    bool initialised_ = false;
};

} // namespace Renderer

#pragma once

#include <bgfx/bgfx.h>

#include "renderer/imgui_window_context.h"
#include "renderer/window.h"

class AudioInput;
struct UIState;

namespace ReSyne {
struct RecorderState;
}

namespace Renderer {

class DetachedVisualisationWindow {
public:
    static constexpr bgfx::ViewId kClearViewId = 2;
    static constexpr bgfx::ViewId kImGuiViewId = 3;

    DetachedVisualisationWindow() = default;
    ~DetachedVisualisationWindow();

    DetachedVisualisationWindow(const DetachedVisualisationWindow&) = delete;
    DetachedVisualisationWindow& operator=(const DetachedVisualisationWindow&) = delete;

    bool open(ReSyne::RecorderState& recorderState);
    void close(ReSyne::RecorderState& recorderState);
    void renderFrame(UIState& state, const AudioInput& audioInput);

    [[nodiscard]] bool isOpen() const;

private:
    bool recreateFrameBufferIfNeeded();
    void destroyFrameBuffer(bool flushSwapChain);

    Window window_;
    ImGuiWindowContext uiContext_;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    void* nativeWindowHandle_ = nullptr;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
};

} // namespace Renderer

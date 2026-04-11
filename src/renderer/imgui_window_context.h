#pragma once

#include <bgfx/bgfx.h>

struct ImGuiContext;
struct ImPlotContext;

namespace Renderer {

class Window;

class ImGuiWindowContext {
public:
    ImGuiWindowContext() = default;
    ~ImGuiWindowContext();

    ImGuiWindowContext(const ImGuiWindowContext&) = delete;
    ImGuiWindowContext& operator=(const ImGuiWindowContext&) = delete;

    bool initialise(const Window& window, bgfx::ViewId viewId);
    void shutdown();

    void makeCurrent() const;
    void beginFrame() const;
    void endFrame() const;
    void renderDrawData() const;

    [[nodiscard]] bool isInitialised() const;

private:
    void destroyContexts();

    ImGuiContext* imguiContext_ = nullptr;
    ImPlotContext* implotContext_ = nullptr;
    bool initialised_ = false;
};

} // namespace Renderer

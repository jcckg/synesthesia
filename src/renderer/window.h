#pragma once

#include <bgfx/platform.h>

struct GLFWwindow;

namespace Renderer {

struct WindowOptions {
    int width = 1480;
    int height = 750;
    const char* title = "Synesthesia";
};

struct FramebufferSize {
    int width = 0;
    int height = 0;
};

bool initialiseWindowing();
void shutdownWindowing();

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const WindowOptions& options);
    void destroy();

    void show() const;
    void pollEvents() const;
    void realiseForRenderer() const;

    [[nodiscard]] bool shouldClose() const;
    [[nodiscard]] GLFWwindow* handle() const;
    [[nodiscard]] FramebufferSize framebufferSize() const;
    [[nodiscard]] bgfx::PlatformData platformData() const;

private:
    GLFWwindow* window_ = nullptr;
};

} // namespace Renderer

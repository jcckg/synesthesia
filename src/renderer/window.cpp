#include "renderer/window.h"
#include "renderer/styling/platform_styling.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3native.h>

namespace Renderer {

namespace {

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description ? description : "unknown");
}

}

bool initialiseWindowing() {
    glfwSetErrorCallback(glfwErrorCallback);
    return glfwInit() == GLFW_TRUE;
}

void shutdownWindowing() {
    glfwTerminate();
}

Window::~Window() {
    destroy();
}

bool Window::create(const WindowOptions& options) {
    minimumWidth_ = options.minimumWidth;
    minimumHeight_ = options.minimumHeight;
    sidePanelGrowth_ = 0;
    sidePanelVisible_ = false;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);

    window_ = glfwCreateWindow(options.width, options.height, options.title, nullptr, nullptr);
    if (window_ == nullptr) return false;
    if (options.minimumWidth > 0 || options.minimumHeight > 0) {
        glfwSetWindowSizeLimits(window_,
            options.minimumWidth > 0 ? options.minimumWidth : GLFW_DONT_CARE,
            options.minimumHeight > 0 ? options.minimumHeight : GLFW_DONT_CARE,
            GLFW_DONT_CARE, GLFW_DONT_CARE);
    }
    return true;
}

void Window::destroy() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
}

void Window::show() const {
    if (window_ == nullptr) {
        return;
    }

    glfwShowWindow(window_);
    glfwFocusWindow(window_);
}

void Window::pollEvents() const {
    glfwPollEvents();
}

void Window::realiseForRenderer() const {
#if defined(__APPLE__)
    for (int i = 0; i < 3; ++i) {
        glfwPollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#endif
    Styling::applyPresentationColourSpace(window_);
}

void Window::setSidePanelVisible(bool visible, bool onLeft, int width) {
    if (window_ == nullptr || visible == sidePanelVisible_) return;
    sidePanelVisible_ = visible;
    const auto restoreLimits = [&] {
        glfwSetWindowSizeLimits(window_, minimumWidth_ > 0 ? minimumWidth_ : GLFW_DONT_CARE,
            minimumHeight_ > 0 ? minimumHeight_ : GLFW_DONT_CARE, GLFW_DONT_CARE, GLFW_DONT_CARE);
    };
    if (glfwGetWindowMonitor(window_) != nullptr || glfwGetWindowAttrib(window_, GLFW_MAXIMIZED)) {
        sidePanelGrowth_ = 0;
        if (!visible) restoreLimits();
        return;
    }
#ifdef __APPLE__
    if (!Styling::allowsAutomaticResize(window_)) {
        sidePanelGrowth_ = 0;
        if (!visible) restoreLimits();
        return;
    }
#endif
    int currentWidth, currentHeight;
    glfwGetWindowSize(window_, &currentWidth, &currentHeight);
    const bool canPosition = glfwGetPlatform() != GLFW_PLATFORM_WAYLAND;
    int x = 0, y = 0;
    int left = 0, top = 0, right = 0, bottom = 0;
    if (canPosition) {
        glfwGetWindowPos(window_, &x, &y);
        glfwGetWindowFrameSize(window_, &left, &top, &right, &bottom);
    }
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    int largestOverlap = 0;
    for (int index = 0; canPosition && index < monitorCount; ++index) {
        int monitorX, monitorY, monitorWidth, monitorHeight;
        glfwGetMonitorWorkarea(monitors[index], &monitorX, &monitorY, &monitorWidth, &monitorHeight);
        const int overlap = std::max(0, std::min(x + currentWidth, monitorX + monitorWidth) - std::max(x, monitorX)) *
                            std::max(0, std::min(y + currentHeight, monitorY + monitorHeight) - std::max(y, monitorY));
        if (overlap > largestOverlap) {
            largestOverlap = overlap;
            monitor = monitors[index];
        }
    }
    int workX = 0, workY = 0, workWidth = currentWidth + width, workHeight = currentHeight;
    if (monitor != nullptr) glfwGetMonitorWorkarea(monitor, &workX, &workY, &workWidth, &workHeight);
    const int maximumWidth = std::max(1, workWidth - left - right);
    int targetWidth;
    if (visible) {
        targetWidth = std::max(currentWidth, std::min(currentWidth + width, maximumWidth));
        sidePanelGrowth_ = targetWidth - currentWidth;
        const int minimum = std::max(minimumWidth_, std::min(minimumWidth_ + width, maximumWidth));
        glfwSetWindowSizeLimits(window_, minimum > 0 ? minimum : GLFW_DONT_CARE,
            minimumHeight_ > 0 ? minimumHeight_ : GLFW_DONT_CARE, GLFW_DONT_CARE, GLFW_DONT_CARE);
    } else {
        restoreLimits();
        targetWidth = std::max(minimumWidth_, currentWidth - sidePanelGrowth_);
        sidePanelGrowth_ = 0;
    }
    if (targetWidth == currentWidth) return;
    int targetX = onLeft ? x + currentWidth - targetWidth : x;
    if (canPosition && monitor != nullptr) {
        targetX = std::clamp(targetX, workX + left, std::max(workX + left, workX + workWidth - right - targetWidth));
    }
    glfwSetWindowSize(window_, targetWidth, currentHeight);
    if (canPosition && targetX != x) glfwSetWindowPos(window_, targetX, y);
}

bool Window::shouldClose() const {
    return window_ == nullptr || glfwWindowShouldClose(window_) != 0;
}

bool Window::isFocused() const {
    return window_ != nullptr && glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
}

GLFWwindow* Window::handle() const {
    return window_;
}

FramebufferSize Window::framebufferSize() const {
    FramebufferSize size;
    if (window_ != nullptr) {
        glfwGetFramebufferSize(window_, &size.width, &size.height);
    }
    return size;
}

bgfx::PlatformData Window::platformData() const {
    bgfx::PlatformData data{};

#if !defined(_WIN32) && !defined(__APPLE__)
    if (window_ != nullptr && glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        data.type = bgfx::NativeWindowHandleType::Wayland;
    }
#endif

    return data;
}

bgfx::SwapChain Window::swapChain() const {
    bgfx::SwapChain data{};

    if (window_ == nullptr) {
        return data;
    }

#if defined(_WIN32)
    data.nwh = glfwGetWin32Window(window_);
#elif defined(__APPLE__)
    data.nwh = glfwGetCocoaWindow(window_);
    data.flags = BGFX_SWAP_CHAIN_HIDPI;
#else
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        data.ndt = glfwGetWaylandDisplay();
        data.nwh = glfwGetWaylandWindow(window_);
    } else {
        data.ndt = glfwGetX11Display();
        data.nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(glfwGetX11Window(window_)));
    }
#endif

    const FramebufferSize size = framebufferSize();
    data.width = static_cast<uint32_t>(size.width);
    data.height = static_cast<uint32_t>(size.height);
    data.formatDepthStencil = bgfx::TextureFormat::D24S8;
    data.numBackBuffers = 2;

    return data;
}

}

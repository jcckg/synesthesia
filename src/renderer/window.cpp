#include "renderer/window.h"
#include "renderer/styling/platform_styling.h"

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

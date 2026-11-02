#include "renderer/imgui_window_context.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "implot.h"

#include "renderer/font_loader.h"
#include "renderer/imgui_impl_bgfx.h"
#include "renderer/render_utils.h"
#include "renderer/window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace Renderer {

ImGuiWindowContext::~ImGuiWindowContext() {
    shutdown();
}

bool ImGuiWindowContext::initialise(const Window& window, const bgfx::ViewId viewId, const bool linearOutput) {
    shutdown();

    IMGUI_CHECKVERSION();
    imguiContext_ = ImGui::CreateContext();
    if (imguiContext_ == nullptr) {
        return false;
    }

    makeCurrent();
    implotContext_ = ImPlot::CreateContext();
    if (implotContext_ == nullptr) {
        destroyContexts();
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    const float dpiScale = uiDpiScale(window);
    loadFonts(io, dpiScale);

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(dpiScale);

    if (!ImGui_ImplGlfw_InitForOther(window.handle(), true)) {
        destroyContexts();
        return false;
    }

    if (!ImGui_Implbgfx_Init(viewId, linearOutput)) {
        ImGui_ImplGlfw_Shutdown();
        destroyContexts();
        return false;
    }

    window_ = &window;
    initialised_ = true;
    return true;
}

void ImGuiWindowContext::shutdown() {
    if (imguiContext_ == nullptr) {
        return;
    }

    makeCurrent();

    if (initialised_) {
        ImGui_Implbgfx_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        initialised_ = false;
    }

    destroyContexts();
}

void ImGuiWindowContext::makeCurrent() const {
    ImGui::SetCurrentContext(imguiContext_);
    ImPlot::SetCurrentContext(implotContext_);
}

void ImGuiWindowContext::beginFrame(bool acceptInput) const {
    makeCurrent();
    ImGuiIO& io = ImGui::GetIO();
    if (!acceptInput) {
        frameConfigFlags_ = io.ConfigFlags;
        inputSuppressed_ = true;
        io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
        io.WantSetMousePos = false;
        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();
    }
    ImGui_Implbgfx_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    if (acceptInput && window_ != nullptr && glfwGetWindowAttrib(window_->handle(), GLFW_HOVERED) &&
        glfwGetInputMode(window_->handle(), GLFW_CURSOR) != GLFW_CURSOR_DISABLED) {
        double x = 0.0, y = 0.0;
        glfwGetCursorPos(window_->handle(), &x, &y);
        io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
    }
    ImGui::NewFrame();
}

void ImGuiWindowContext::endFrame() const {
    makeCurrent();
    ImGui::Render();
    if (inputSuppressed_) {
        ImGui::GetIO().ConfigFlags = frameConfigFlags_;
        inputSuppressed_ = false;
    }
}

void ImGuiWindowContext::renderDrawData() const {
    makeCurrent();
    ImGui_Implbgfx_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiWindowContext::isInitialised() const {
    return initialised_;
}

void ImGuiWindowContext::destroyContexts() {
    window_ = nullptr;
    if (implotContext_ != nullptr) {
        ImPlot::DestroyContext(implotContext_);
        implotContext_ = nullptr;
    }

    if (imguiContext_ != nullptr) {
        ImGui::DestroyContext(imguiContext_);
        imguiContext_ = nullptr;
    }
}

}

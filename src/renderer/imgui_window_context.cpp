#include "renderer/imgui_window_context.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "implot.h"

#include "renderer/font_loader.h"
#include "renderer/imgui_impl_bgfx.h"
#include "renderer/render_utils.h"
#include "renderer/window.h"

namespace Renderer {

ImGuiWindowContext::~ImGuiWindowContext() {
    shutdown();
}

bool ImGuiWindowContext::initialise(const Window& window, const bgfx::ViewId viewId) {
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

    if (!ImGui_Implbgfx_Init(viewId)) {
        ImGui_ImplGlfw_Shutdown();
        destroyContexts();
        return false;
    }

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

void ImGuiWindowContext::beginFrame() const {
    makeCurrent();
    ImGui_Implbgfx_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiWindowContext::endFrame() const {
    makeCurrent();
    ImGui::Render();
}

void ImGuiWindowContext::renderDrawData() const {
    makeCurrent();
    ImGui_Implbgfx_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiWindowContext::isInitialised() const {
    return initialised_;
}

void ImGuiWindowContext::destroyContexts() {
    if (implotContext_ != nullptr) {
        ImPlot::DestroyContext(implotContext_);
        implotContext_ = nullptr;
    }

    if (imguiContext_ != nullptr) {
        ImGui::DestroyContext(imguiContext_);
        imguiContext_ = nullptr;
    }
}

} // namespace Renderer

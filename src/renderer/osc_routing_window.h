#pragma once

#include "renderer/imgui_window_context.h"
#include "renderer/window.h"
#include "osc/osc_routing.h"
#include "ui/input/scrolling.h"
#include "ui/menu/menu_bar.h"

#include <bgfx/bgfx.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace pfd {
class open_file;
class save_file;
}

struct UIState;

namespace Renderer {

class OSCRoutingWindow {
public:
    OSCRoutingWindow();
    ~OSCRoutingWindow();
    bool open(bgfx::TextureFormat::Enum colourFormat);
    void close();
    void renderFrame(UIState& state);
    bool isOpen() const;
    bool isCommandTarget(UI::Menu::Command command) const;
    UI::Menu::State queryCommand(UI::Menu::Command command) const;
    bool executeCommand(UI::Menu::Command command);

private:
    bool refreshFrameBuffer();
    void renderContents(UIState& state);
    void pollDialogs(UIState& state);
    void openLoadDialog();

    Window window_;
    ImGuiWindowContext uiContext_;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::TextureFormat::Enum colourFormat_ = bgfx::TextureFormat::BGRA8;
    void* nativeWindowHandle_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    std::array<char, 192> search_{};
    bool enabledOnly_ = false;
    bool searchFocusRequested_ = false;
    UI::Input::Scrolling scrolling_;
    UI::Input::ScrollMotion scroll_;
    ImGuiSelectionBasicStorage selection_;
    std::vector<std::size_t> visibleRows_;
    std::string filteredSearch_;
    std::vector<bool> filteredEnabled_;
    bool filteredEnabledOnly_ = false;
    bool filterValid_ = false;
    struct OutputSizeKey {
        std::size_t type = 0, length = 0;
        bool vector = false, available = false;
        bool operator==(const OutputSizeKey&) const = default;
    };
    std::vector<OutputSizeKey> outputSizeKeys_;
    std::vector<Synesthesia::OSC::OSCOutputSize> outputSizes_;
    std::string message_;
    std::string configurationPath_;
    bool error_ = false;
    float messageRemaining_ = 0.0f;
    std::unique_ptr<pfd::open_file> loadDialog_;
    std::unique_ptr<pfd::save_file> saveDialog_;
};

}

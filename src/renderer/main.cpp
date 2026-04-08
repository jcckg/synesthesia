#include "renderer/bgfx_context.h"
#include "renderer/font_loader.h"
#include "renderer/imgui_impl_bgfx.h"
#include "renderer/styling/platform_styling.h"
#include "renderer/window.h"

#include "audio_input.h"
#include "audio_output.h"
#include "ui.h"
#include "ui/dragdrop/file_drop_manager.h"
#include "ui/input/trackpad_gestures.h"
#include "ui/styling/system_theme/system_theme_detector.h"
#include "utilities/video/ffmpeg_locator.h"

#ifdef ENABLE_MIDI
#include "midi_input.h"
#include "midi_device_manager.h"
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "implot.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr int kDefaultWindowWidth = 1480;
constexpr int kDefaultWindowHeight = 750;
constexpr int kTargetFramesPerSecond = 120;
constexpr int kTargetFrameDurationMicroseconds = 1'000'000 / kTargetFramesPerSecond;

void getThemeBackgroundColour(float* colour) {
    const SystemTheme theme = SystemThemeDetector::detectSystemTheme();
    if (theme == SystemTheme::Light) {
        colour[0] = 1.0f;
        colour[1] = 1.0f;
        colour[2] = 1.0f;
        colour[3] = 1.0f;
    } else {
        colour[0] = 0.0f;
        colour[1] = 0.0f;
        colour[2] = 0.0f;
        colour[3] = 1.0f;
    }
}

uint32_t packRgba8(float red, float green, float blue, float alpha) {
    const auto toByte = [](float value) -> uint8_t {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
    };

    const uint32_t rr = static_cast<uint32_t>(toByte(red));
    const uint32_t gg = static_cast<uint32_t>(toByte(green));
    const uint32_t bb = static_cast<uint32_t>(toByte(blue));
    const uint32_t aa = static_cast<uint32_t>(toByte(alpha));
    return (rr << 24) | (gg << 16) | (bb << 8) | aa;
}

float uiDpiScale(const Renderer::Window& window) {
    const float rawScale = std::max(1.0f, ImGui_ImplGlfw_GetContentScaleForWindow(window.handle()));
#if defined(_WIN32)
    return 1.0f + (rawScale - 1.0f) * 0.65f;
#else
    return rawScale;
#endif
}

#ifdef ENABLE_MIDI
void initialiseMidiState(UIState& uiState, MIDIInput& midiInput, std::vector<MIDIInput::DeviceInfo>& midiDevices) {
    uiState.midiDevicesAvailable = !midiDevices.empty();
    if (!uiState.midiDevicesAvailable) {
        return;
    }

    MIDIDeviceManager::populateMIDIDeviceNames(uiState.midiDeviceState, midiDevices);
    if (MIDIDeviceManager::autoDetectAndConnect(uiState.midiDeviceState, midiInput, midiDevices)) {
        uiState.midiAutoConnected = true;
        std::fprintf(stderr, "[MIDI] Auto-connected on start-up\n");
    }
}
#endif

} // namespace

int app_main(int, char**) {
    if (!Renderer::initialiseWindowing()) {
        std::fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }

    Renderer::Window window;
    Renderer::WindowOptions options;
    options.width = kDefaultWindowWidth;
    options.height = kDefaultWindowHeight;

    if (!window.create(options)) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        Renderer::shutdownWindowing();
        return 1;
    }

    Renderer::Styling::applyPlatformWindowStyling(window.handle());
    window.show();
    window.realiseForRenderer();

    const Renderer::FramebufferSize initialSize = window.framebufferSize();

    Renderer::BgfxContext bgfxContext;
    if (!bgfxContext.initialise(
            window,
            static_cast<uint32_t>(std::max(initialSize.width, 1)),
            static_cast<uint32_t>(std::max(initialSize.height, 1)))) {
        std::fprintf(stderr, "Failed to initialise bgfx\n");
        window.destroy();
        Renderer::shutdownWindowing();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    const float dpiScale = uiDpiScale(window);
    Renderer::loadFonts(io, dpiScale);

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(dpiScale);

    if (!ImGui_ImplGlfw_InitForOther(window.handle(), true)) {
        std::fprintf(stderr, "Failed to initialise ImGui GLFW backend\n");
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        bgfxContext.shutdown();
        window.destroy();
        Renderer::shutdownWindowing();
        return 1;
    }

    if (!ImGui_Implbgfx_Init(Renderer::BgfxContext::kImGuiViewId)) {
        std::fprintf(
            stderr,
            "Failed to initialise ImGui bgfx backend (renderer=%s)\n",
            bgfx::getRendererName(bgfxContext.rendererType())
        );
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        bgfxContext.shutdown();
        window.destroy();
        Renderer::shutdownWindowing();
        return 1;
    }

    FileDropManager::attach(window.handle());
    TrackpadGestures::attach(window.handle());

    auto& ffmpegLocator = Utilities::Video::FFmpegLocator::instance();
    ffmpegLocator.refresh();

    AudioInput audioInput;
    std::vector<AudioInput::DeviceInfo> inputDevices = AudioInput::getInputDevices();
    std::vector<AudioOutput::DeviceInfo> outputDevices = AudioOutput::getOutputDevices();

#ifdef ENABLE_MIDI
    MIDIInput midiInput;
    std::vector<MIDIInput::DeviceInfo> midiDevices = MIDIInput::getMIDIInputDevices();
#endif

    float clearColour[4];
    getThemeBackgroundColour(clearColour);

    UIState uiState;

#ifdef ENABLE_MIDI
    initialiseMidiState(uiState, midiInput, midiDevices);
#endif

    constexpr auto targetFrameDuration = std::chrono::microseconds(kTargetFrameDurationMicroseconds);

    int previousWidth = std::max(initialSize.width, 1);
    int previousHeight = std::max(initialSize.height, 1);

    while (!window.shouldClose()) {
        const auto frameStart = std::chrono::steady_clock::now();
        window.pollEvents();

        const Renderer::FramebufferSize currentSize = window.framebufferSize();
        const int framebufferWidth = currentSize.width;
        const int framebufferHeight = currentSize.height;

        if (framebufferWidth <= 0 || framebufferHeight <= 0) {
            bgfx::frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (framebufferWidth != previousWidth || framebufferHeight != previousHeight) {
            bgfxContext.reset(static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight));
            previousWidth = framebufferWidth;
            previousHeight = framebufferHeight;
        }

        bgfxContext.setViewRects(static_cast<uint16_t>(framebufferWidth), static_cast<uint16_t>(framebufferHeight));

        ImGui_Implbgfx_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        updateUI(audioInput, inputDevices, outputDevices, clearColour, io, uiState
#ifdef ENABLE_MIDI
                 , &midiInput, &midiDevices
#endif
        );

        ImGui::Render();

        const uint32_t packedClear = packRgba8(clearColour[0], clearColour[1], clearColour[2], clearColour[3]);
        bgfx::setViewClear(Renderer::BgfxContext::kClearViewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, packedClear, 1.0f, 0);
        bgfx::touch(Renderer::BgfxContext::kClearViewId);

        ImGui_Implbgfx_RenderDrawData(ImGui::GetDrawData());
        bgfx::frame();

        const auto frameEnd = std::chrono::steady_clock::now();
        const auto frameDuration = frameEnd - frameStart;
        if (frameDuration < targetFrameDuration) {
            std::this_thread::sleep_for(targetFrameDuration - frameDuration);
        }
    }

    ImGui_Implbgfx_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    TrackpadGestures::shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    bgfxContext.shutdown();
    window.destroy();
    Renderer::shutdownWindowing();

    return 0;
}

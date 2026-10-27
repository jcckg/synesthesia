#include "renderer/bgfx_context.h"
#include "renderer/detached_visualisation_window.h"
#include "renderer/event_tracking_frames.h"
#include "renderer/imgui_window_context.h"
#include "renderer/presentation_resources.h"
#include "renderer/render_utils.h"
#include "renderer/styling/platform_styling.h"
#include "renderer/window.h"

#include "audio_input.h"
#include "audio_output.h"
#include "ui.h"
#include "ui/dragdrop/file_drop_manager.h"
#include "ui/input/trackpad_gestures.h"
#include "ui/input/shortcuts.h"
#include "ui/input/continuous_pan.h"
#include "ui/menu/application_menu.h"
#include "ui/styling/system_theme/system_theme_detector.h"
#include "utilities/video/ffmpeg_locator.h"

#ifdef ENABLE_MIDI
#include "midi_input.h"
#include "midi_device_manager.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr int kDefaultWindowWidth = 1480;
constexpr int kDefaultWindowHeight = 750;
constexpr int kMinimumWindowWidth = 900;
constexpr int kMinimumWindowHeight = 550;
constexpr int kTargetFramesPerSecond = 120;
constexpr int kTargetFrameDurationMicroseconds = 1'000'000 / kTargetFramesPerSecond;

struct FrameScope {
    bool& rendering;
    bool& tracking;

    FrameScope(bool& active, bool& nativeTracking, bool isTracking)
        : rendering(active), tracking(nativeTracking) {
        rendering = true;
        tracking = isTracking;
    }

    ~FrameScope() {
        tracking = false;
        rendering = false;
    }
};

PresentationDiagnostics::DisplaySurfacePrecision displaySurfacePrecisionFromFormat(
    const bgfx::TextureFormat::Enum colourFormat) {
    switch (colourFormat) {
        case bgfx::TextureFormat::RGB10A2:
            return PresentationDiagnostics::DisplaySurfacePrecision::TenBit;
        case bgfx::TextureFormat::BGRA8:
            return PresentationDiagnostics::DisplaySurfacePrecision::EightBit;
        default:
            return PresentationDiagnostics::DisplaySurfacePrecision::Unknown;
    }
}

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

}

int app_main(int, char**) {
    if (!Renderer::initialiseWindowing()) {
        std::fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }

    Renderer::Window window;
    Renderer::WindowOptions options;
    options.width = kDefaultWindowWidth;
    options.height = kDefaultWindowHeight;
    options.minimumWidth = kMinimumWindowWidth;
    options.minimumHeight = kMinimumWindowHeight;

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

    Renderer::ImGuiWindowContext mainWindowContext;
    if (!mainWindowContext.initialise(window, Renderer::BgfxContext::kImGuiViewId, bgfxContext.usesLinearPresentation())) {
        std::fprintf(stderr, "Failed to initialise ImGui window context\n");
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
    uiState.presentationDiagnostics.displaySurfacePrecision =
        displaySurfacePrecisionFromFormat(bgfxContext.colourFormat());
    Renderer::PresentationResources presentationResources;
    if (!presentationResources.initialise()) {
        std::fprintf(stderr, "[renderer] Failed to initialise presentation resources, using standard colour widgets\n");
    } else {
        uiState.presentationResources = &presentationResources;
        uiState.presentationDiagnostics.presentationResourcesAvailable = true;
        uiState.presentationDiagnostics.highPrecisionTexturesAvailable =
            presentationResources.supportsHighPrecisionTextures();
        uiState.presentationDiagnostics.backgroundPresentationAvailable =
            presentationResources.supportsBackgroundPresentation();
        if (!presentationResources.supportsBackgroundPresentation()) {
            std::fprintf(stderr, "[renderer] Presentation background pass unavailable, using clear-colour fallback for fullscreen backgrounds\n");
        }
    }
    auto& recorderState = uiState.resyneState.recorderState;
    recorderState.presentationResources = uiState.presentationResources;
    recorderState.detachedVisualisation.available = bgfxContext.supportsMultipleWindows();
    Renderer::DetachedVisualisationWindow detachedVisualisationWindow;
    UI::Menu::MenuBar menuBar(UI::Menu::applicationMenus(),
        [&uiState](UI::Menu::Command command) {
            return UI::Menu::queryCommand(command, uiState);
        },
        [&uiState, &window](UI::Menu::Command command) {
            if (UI::Menu::executeCommand(command, uiState) && command != UI::Menu::Command::ToggleOSC &&
                command != UI::Menu::Command::ToggleSpectrumAnalyser && command != UI::Menu::Command::ProjectPage) {
                window.show();
            }
        });

#ifdef ENABLE_MIDI
    initialiseMidiState(uiState, midiInput, midiDevices);
#endif

    constexpr auto targetFrameDuration = std::chrono::microseconds(kTargetFrameDurationMicroseconds);

    int previousWidth = std::max(initialSize.width, 1);
    int previousHeight = std::max(initialSize.height, 1);
    UI::Input::ContinuousPan continuousPan(window.handle());

    const auto canPanTimeline = [&] {
        return uiState.visibility.showUI && !uiState.helpState.showKeyboardShortcuts &&
               recorderState.importPhase == 0 && recorderState.timeline.gradientRegionValid &&
               (uiState.visualSettings.activeView == UIState::View::ReSyne || recorderState.windowOpen) &&
               !recorderState.detachedVisualisation.openRequested && !recorderState.detachedVisualisation.reattachRequested &&
               !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    };

    bool renderingFrame = false;
    const auto renderFrame = [&](bool nativeTracking) {
        if (renderingFrame || window.shouldClose()) return;
        FrameScope frameScope(renderingFrame, uiState.nativeMenuTracking, nativeTracking);
        mainWindowContext.makeCurrent();
        if (!nativeTracking) {
            auto& browser = recorderState.fileBrowser;
            UI::FileBrowser::advancePanel(browser, ImGui::GetIO().DeltaTime, ImGui::GetFrameCount() + 1);
            if (!browser.resizing.active) window.setSidePanelVisible(browser.isOpen, !uiState.visibility.sidebarOnLeft,
                static_cast<int>(std::round(browser.resizing.width)), browser.panelAnimation.value);
            browser.expandedWindowWidth = static_cast<float>(window.sidePanelExpandedWidth());
        }
        continuousPan.synchronise(recorderState.timeline, recorderState.toolState, !nativeTracking && canPanTimeline(), false);

        const Renderer::FramebufferSize currentSize = window.framebufferSize();
        const int framebufferWidth = currentSize.width;
        const int framebufferHeight = currentSize.height;
        const bool mainWindowVisible = framebufferWidth > 0 && framebufferHeight > 0;

        if (mainWindowVisible && (framebufferWidth != previousWidth || framebufferHeight != previousHeight)) {
            bgfxContext.reset(static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight));
            previousWidth = framebufferWidth;
            previousHeight = framebufferHeight;
        }

        if (mainWindowVisible) {
            bgfxContext.setViewRects(static_cast<uint16_t>(framebufferWidth), static_cast<uint16_t>(framebufferHeight));
        }

        UI::FileBrowser::uploadArtwork(recorderState.fileBrowser);
        mainWindowContext.beginFrame(!nativeTracking);
        if (!nativeTracking) menuBar.dispatchPendingCommands();
        if (!nativeTracking && !recorderState.fileBrowser.requestedFile.empty()) {
            if (UI::FileBrowser::canLoadFiles(uiState)) {
                ReSyne::Recorder::requestImport(recorderState, recorderState.fileBrowser.requestedFile);
                uiState.visualSettings.activeView = UIState::View::ReSyne;
                uiState.visibility.showUI = true;
                recorderState.focusRequested = true;
            }
            recorderState.fileBrowser.requestedFile.clear();
        }
        if (!nativeTracking && !uiState.helpState.showKeyboardShortcuts) {
            UI::Input::handlePlaybackShortcut(recorderState, window.isFocused());
        }

        updateUI(audioInput, inputDevices, outputDevices, clearColour, ImGui::GetIO(), uiState
#ifdef ENABLE_MIDI
                 , &midiInput, &midiDevices
#endif
        );

        continuousPan.synchronise(recorderState.timeline, recorderState.toolState, !nativeTracking && canPanTimeline(), true);
        mainWindowContext.endFrame();

        if (!nativeTracking && recorderState.detachedVisualisation.reattachRequested) {
            detachedVisualisationWindow.close(recorderState);
            mainWindowContext.makeCurrent();
            uiState.visualSettings.activeView = UIState::View::ReSyne;
            uiState.visibility.showUI = true;
        }

        if (!nativeTracking && recorderState.detachedVisualisation.openRequested && !recorderState.detachedVisualisation.isOpen) {
            uiState.visualSettings.activeView = UIState::View::ReSyne;
            uiState.visibility.showUI = true;

            if (!recorderState.detachedVisualisation.available) {
                recorderState.detachedVisualisation.openRequested = false;
                recorderState.statusMessage = "Detached visualisation is not supported by the current renderer";
                recorderState.statusMessageTimer = 4.0f;
            } else if (!detachedVisualisationWindow.open(recorderState, bgfxContext.colourFormat())) {
                recorderState.statusMessage = "Unable to open detached visualisation window";
                recorderState.statusMessageTimer = 4.0f;
            }

            mainWindowContext.makeCurrent();
        }

        if (mainWindowVisible) {
            const bool usedPresentationBackground =
                uiState.presentationResources != nullptr &&
                uiState.presentationResources->submitBackground(
                    Renderer::BgfxContext::kBackgroundViewId,
                    static_cast<uint16_t>(framebufferWidth),
                    static_cast<uint16_t>(framebufferHeight),
                    clearColour[0],
                    clearColour[1],
                    clearColour[2]);
            const uint32_t packedClear = usedPresentationBackground
                ? Renderer::packRgba8(0.0f, 0.0f, 0.0f, clearColour[3])
                : Renderer::packRgba8(clearColour[0], clearColour[1], clearColour[2], clearColour[3]);
            bgfx::setViewClear(Renderer::BgfxContext::kClearViewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, packedClear, 1.0f, 0);
            bgfx::touch(Renderer::BgfxContext::kClearViewId);
            mainWindowContext.renderDrawData();
        }

        if (recorderState.detachedVisualisation.isOpen) {
            detachedVisualisationWindow.renderFrame(uiState, audioInput);
            mainWindowContext.makeCurrent();
        }

        bgfx::frame();
    };

    Renderer::EventTrackingFrames trackingFrames([&] { renderFrame(true); }, kTargetFramesPerSecond);

    while (!window.shouldClose()) {
        const auto frameStart = std::chrono::steady_clock::now();
        window.pollEvents();
        trackingFrames.rethrowPendingException();
        renderFrame(false);

        const auto frameEnd = std::chrono::steady_clock::now();
        const auto frameDuration = frameEnd - frameStart;
        if (frameDuration < targetFrameDuration) {
            std::this_thread::sleep_for(targetFrameDuration - frameDuration);
        }
    }

    trackingFrames.shutdown();
    continuousPan.release();
    if (recorderState.detachedVisualisation.isOpen) {
        detachedVisualisationWindow.close(recorderState);
        mainWindowContext.makeCurrent();
    }

    menuBar.shutdown();
    UI::FileBrowser::releaseArtwork(recorderState.fileBrowser);
    presentationResources.shutdown();
    TrackpadGestures::shutdown();
    mainWindowContext.shutdown();
    bgfxContext.shutdown();
    window.destroy();
    Renderer::shutdownWindowing();

    return 0;
}

#include "renderer/detached_visualisation_window.h"

#include "audio/input/audio_input.h"
#include "imgui.h"
#include "renderer/presentation_resources.h"
#include "renderer/render_utils.h"
#include "renderer/styling/platform_styling.h"
#include "resyne/recorder/recorder.h"
#include "ui.h"
#include "ui/audio_visualisation/presentation_state.h"
#include "ui/audio_visualisation/visualisation_surface.h"

namespace Renderer {

namespace {

constexpr int kDefaultWindowWidth = 1280;
constexpr int kDefaultWindowHeight = 720;

}

DetachedVisualisationWindow::~DetachedVisualisationWindow() {
    if (bgfx::isValid(frameBuffer_)) {
        bgfx::destroy(frameBuffer_);
        frameBuffer_ = BGFX_INVALID_HANDLE;
    }
}

bool DetachedVisualisationWindow::open(ReSyne::RecorderState& recorderState,
                                       const bgfx::TextureFormat::Enum colourFormat) {
    recorderState.detachedVisualisation.openRequested = false;
    colourFormat_ = colourFormat;

    if (isOpen()) {
        recorderState.detachedVisualisation.isOpen = true;
        return true;
    }

    WindowOptions options;
    options.width = kDefaultWindowWidth;
    options.height = kDefaultWindowHeight;
    options.title = "Synesthesia Visualisation";

    if (!window_.create(options)) {
        return false;
    }

    Styling::applyPlatformWindowStyling(window_.handle());
    window_.show();
    window_.realiseForRenderer();

    if (!uiContext_.initialise(window_, kImGuiViewId, colourFormat_ == bgfx::TextureFormat::RGBA16F)) {
        window_.destroy();
        return false;
    }

    if (!recreateFrameBufferIfNeeded()) {
        uiContext_.shutdown();
        window_.destroy();
        return false;
    }

    recorderState.detachedVisualisation.isOpen = true;
    return true;
}

void DetachedVisualisationWindow::close(ReSyne::RecorderState& recorderState) {
    if (!isOpen()) {
        recorderState.detachedVisualisation.isOpen = false;
        recorderState.detachedVisualisation.openRequested = false;
        return;
    }

    uiContext_.shutdown();
    destroyFrameBuffer(true);
    window_.destroy();

    nativeWindowHandle_ = nullptr;
    framebufferWidth_ = 0;
    framebufferHeight_ = 0;

    recorderState.detachedVisualisation.isOpen = false;
    recorderState.detachedVisualisation.openRequested = false;
}

void DetachedVisualisationWindow::renderFrame(UIState& state, const AudioInput& audioInput) {
    auto& recorderState = state.resyneState.recorderState;
    if (!isOpen()) {
        recorderState.detachedVisualisation.isOpen = false;
        return;
    }

    if (window_.shouldClose()) {
        close(recorderState);
        return;
    }

    const FramebufferSize size = window_.framebufferSize();
    if (size.width <= 0 || size.height <= 0) {
        return;
    }

    if (!recreateFrameBufferIfNeeded()) {
        return;
    }

    uiContext_.beginFrame();

    UI::AudioVisualisation::SurfaceLayout layout;
    layout.displaySize = ImGui::GetIO().DisplaySize;

    UI::AudioVisualisation::renderSpectrumOverlay(
        state,
        audioInput,
        layout,
        UI::AudioVisualisation::hasPlaybackSession(recorderState)
    );

    uiContext_.endFrame();

    const auto clearColour = UI::AudioVisualisation::currentVisualisationClearColour(state);
    bgfx::setViewFrameBuffer(kClearViewId, frameBuffer_);
    bgfx::setViewRect(
        kClearViewId,
        0,
        0,
        static_cast<uint16_t>(size.width),
        static_cast<uint16_t>(size.height)
    );
    bool usedPresentationBackground = false;
    if (state.presentationResources != nullptr) {
        bgfx::setViewFrameBuffer(kBackgroundViewId, frameBuffer_);
        usedPresentationBackground = state.presentationResources->submitBackground(
            kBackgroundViewId,
            static_cast<uint16_t>(size.width),
            static_cast<uint16_t>(size.height),
            clearColour[0],
            clearColour[1],
            clearColour[2]);
    }
    const uint32_t packedClear = usedPresentationBackground
        ? packRgba8(0.0f, 0.0f, 0.0f, clearColour[3])
        : packRgba8(clearColour[0], clearColour[1], clearColour[2], clearColour[3]);
    bgfx::setViewClear(
        kClearViewId,
        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
        packedClear,
        1.0f,
        0
    );
    bgfx::touch(kClearViewId);

    bgfx::setViewFrameBuffer(kImGuiViewId, frameBuffer_);
    uiContext_.renderDrawData();
}

bool DetachedVisualisationWindow::isOpen() const {
    return window_.handle() != nullptr && uiContext_.isInitialised();
}

bool DetachedVisualisationWindow::recreateFrameBufferIfNeeded() {
    const FramebufferSize size = window_.framebufferSize();
    if (size.width <= 0 || size.height <= 0) {
        return false;
    }

    const bgfx::PlatformData platformData = window_.platformData();
    if (platformData.nwh == nullptr) {
        return false;
    }

    if (bgfx::isValid(frameBuffer_) &&
        nativeWindowHandle_ == platformData.nwh &&
        framebufferWidth_ == size.width &&
        framebufferHeight_ == size.height) {
        return true;
    }

    destroyFrameBuffer(false);

    nativeWindowHandle_ = platformData.nwh;
    framebufferWidth_ = size.width;
    framebufferHeight_ = size.height;
    frameBuffer_ = bgfx::createFrameBuffer(
        nativeWindowHandle_,
        static_cast<uint16_t>(framebufferWidth_),
        static_cast<uint16_t>(framebufferHeight_),
        colourFormat_,
        bgfx::TextureFormat::D24S8
    );
    return bgfx::isValid(frameBuffer_);
}

void DetachedVisualisationWindow::destroyFrameBuffer(const bool flushSwapChain) {
    if (!bgfx::isValid(frameBuffer_)) {
        return;
    }

    bgfx::destroy(frameBuffer_);
    frameBuffer_ = BGFX_INVALID_HANDLE;

    if (!flushSwapChain) {
        bgfx::frame();
        return;
    }

    bgfx::frame();
    bgfx::frame();
}

} // namespace Renderer

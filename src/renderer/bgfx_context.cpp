#include "renderer/bgfx_context.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <bgfx/platform.h>

#include "renderer/window.h"

namespace Renderer {

namespace {

constexpr uint32_t kTransientVertexBufferLimit = 16u * 1024u * 1024u;
constexpr uint32_t kTransientIndexBufferLimit = 8u * 1024u * 1024u;
constexpr std::array kPreferredColourFormats{
    bgfx::TextureFormat::RGB10A2,
    bgfx::TextureFormat::BGRA8
};

bgfx::RendererType::Enum rendererTypeFromString(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (lower == "auto") {
        return bgfx::RendererType::Count;
    }
    if (lower == "metal") {
        return bgfx::RendererType::Metal;
    }
    if (lower == "vulkan" || lower == "vk") {
        return bgfx::RendererType::Vulkan;
    }
    if (lower == "d3d12" || lower == "direct3d12" || lower == "dx12") {
        return bgfx::RendererType::Direct3D12;
    }
    if (lower == "d3d11" || lower == "direct3d11" || lower == "dx11") {
        return bgfx::RendererType::Direct3D11;
    }
    if (lower == "gl" || lower == "opengl") {
        return bgfx::RendererType::OpenGL;
    }

    return bgfx::RendererType::Noop;
}

bgfx::RendererType::Enum rendererTypeFromEnvironment() {
    const char* env = std::getenv("SYN_BGFX_RENDERER");
    if (env == nullptr || env[0] == '\0') {
        return bgfx::RendererType::Count;
    }

    const bgfx::RendererType::Enum type = rendererTypeFromString(env);
    if (type == bgfx::RendererType::Noop) {
        std::fprintf(stderr, "[bgfx] Ignoring unknown SYN_BGFX_RENDERER='%s'\n", env);
        return bgfx::RendererType::Count;
    }

    return type;
}

} // namespace

BgfxContext::BgfxContext() {
#if defined(__APPLE__)
    reset_flags_ |= BGFX_RESET_HIDPI;
#endif
}

bool BgfxContext::initialise(const Window& window, uint32_t width, uint32_t height) {
    if (initialised_) {
        return true;
    }

#if defined(__APPLE__)
    // bgfx documentation and long-standing example code keep renderer startup on the API thread on macOS.
    bgfx::renderFrame(0);
#endif

    for (const auto colourFormat : kPreferredColourFormats) {
        bgfx::Init init;
        init.type = rendererTypeFromEnvironment();
        init.fallback = true;
        init.resolution.width = std::max(width, 1u);
        init.resolution.height = std::max(height, 1u);
        init.resolution.reset = reset_flags_;
        init.resolution.formatColor = colourFormat;
        init.platformData = window.platformData();
        init.limits.maxTransientVbSize = kTransientVertexBufferLimit;
        init.limits.maxTransientIbSize = kTransientIndexBufferLimit;

        if (!bgfx::init(init)) {
            continue;
        }

        if (bgfx::getRendererType() == bgfx::RendererType::Noop) {
            bgfx::shutdown();
            continue;
        }

        colour_format_ = colourFormat;
        initialised_ = true;
        setViewRects(static_cast<uint16_t>(std::max(width, 1u)), static_cast<uint16_t>(std::max(height, 1u)));
        return true;
    }

    return false;
}

void BgfxContext::shutdown() {
    if (!initialised_) {
        return;
    }

    bgfx::shutdown();
    initialised_ = false;
}

void BgfxContext::reset(uint32_t width, uint32_t height) {
    if (!initialised_) {
        return;
    }

    bgfx::reset(std::max(width, 1u), std::max(height, 1u), reset_flags_, colour_format_);
}

void BgfxContext::setViewRects(uint16_t width, uint16_t height) const {
    if (!initialised_) {
        return;
    }

    bgfx::setViewRect(kClearViewId, 0, 0, width, height);
    bgfx::setViewRect(kBackgroundViewId, 0, 0, width, height);
    bgfx::setViewRect(kImGuiViewId, 0, 0, width, height);
}

bool BgfxContext::isInitialised() const {
    return initialised_;
}

uint32_t BgfxContext::resetFlags() const {
    return reset_flags_;
}

bgfx::RendererType::Enum BgfxContext::rendererType() const {
    return initialised_ ? bgfx::getRendererType() : bgfx::RendererType::Noop;
}

bool BgfxContext::supportsMultipleWindows() const {
    if (!initialised_) {
        return false;
    }

    const bgfx::Caps* caps = bgfx::getCaps();
    return caps != nullptr && (caps->supported & BGFX_CAPS_SWAP_CHAIN) != 0;
}

bgfx::TextureFormat::Enum BgfxContext::colourFormat() const {
    return colour_format_;
}

bool BgfxContext::usesLinearPresentation() const {
    return colour_format_ == bgfx::TextureFormat::RGBA16F;
}

} // namespace Renderer

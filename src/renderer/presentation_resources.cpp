#include "renderer/presentation_resources.h"

#include <algorithm>
#include <array>
#include <span>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include <bgfx/embedded_shader.h>
#include <bx/math.h>
#include <bx/uint32_t.h>

#include "../../vendor/bgfx/examples/common/imgui/fs_ocornut_imgui.bin.h"
#include "../../vendor/bgfx/examples/common/imgui/vs_ocornut_imgui.bin.h"

#include "colour/colour_presentation.h"
#include "resyne/ui/timeline/timeline_rasteriser.h"

namespace Renderer {

namespace {

struct FullscreenVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint32_t abgr = UINT32_MAX;

    static bgfx::VertexLayout layout;

    static void initialiseLayout() {
        if (layout.has(bgfx::Attrib::Position)) {
            return;
        }

        layout
            .begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
    }
};

bgfx::VertexLayout FullscreenVertex::layout;

const bgfx::EmbeddedShader kEmbeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER_END()
};

ImTextureID textureIdFromHandle(const bgfx::TextureHandle handle) {
    if (!bgfx::isValid(handle)) {
        return ImTextureID_Invalid;
    }

    return static_cast<ImTextureID>(handle.idx);
}
 
} // namespace

class PresentationResources::SampledTexture {
public:
    explicit SampledTexture(const char* name)
        : name_(name) {
    }

    ~SampledTexture() {
        shutdown();
    }

    void shutdown() {
        if (!bgfx::isValid(texture_)) {
            return;
        }

        bgfx::destroy(texture_);
        texture_ = BGFX_INVALID_HANDLE;
        width_ = 0;
        height_ = 0;
    }

    bool update(const std::span<const float> rgbaPixels,
                const uint16_t width,
                const uint16_t height,
                const bgfx::TextureFormat::Enum format) {
        if (rgbaPixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
            return false;
        }

        if (!ensureTexture(width, height, format)) {
            return false;
        }

        const bgfx::Memory* memory = nullptr;
        if (format_ == bgfx::TextureFormat::RGBA16F) {
            halfPixels_.resize(rgbaPixels.size());
            for (std::size_t index = 0; index < rgbaPixels.size(); ++index) {
                halfPixels_[index] = bx::halfFromFloat(rgbaPixels[index]);
            }
            memory = bgfx::copy(halfPixels_.data(), static_cast<uint32_t>(halfPixels_.size() * sizeof(std::uint16_t)));
        } else {
            bytePixels_.resize(rgbaPixels.size());
            for (std::size_t index = 0; index < rgbaPixels.size(); ++index) {
                bytePixels_[index] = static_cast<std::uint8_t>(
                    std::clamp(rgbaPixels[index], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            memory = bgfx::copy(bytePixels_.data(), static_cast<uint32_t>(bytePixels_.size()));
        }

        if (memory == nullptr) {
            return false;
        }

        bgfx::updateTexture2D(texture_, 0, 0, 0, 0, width, height, memory);
        return true;
    }

    [[nodiscard]] ImTextureID textureId() const {
        return textureIdFromHandle(texture_);
    }

    [[nodiscard]] bgfx::TextureHandle handle() const {
        return texture_;
    }

private:
    bool ensureTexture(const uint16_t width,
                       const uint16_t height,
                       const bgfx::TextureFormat::Enum format) {
        if (bgfx::isValid(texture_) &&
            width_ == width &&
            height_ == height &&
            format_ == format) {
            return true;
        }

        shutdown();

        texture_ = bgfx::createTexture2D(
            width,
            height,
            false,
            1,
            format,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
        );
        if (!bgfx::isValid(texture_)) {
            return false;
        }

        bgfx::setName(texture_, name_);
        width_ = width;
        height_ = height;
        format_ = format;
        return true;
    }

    const char* name_ = nullptr;
    bgfx::TextureHandle texture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureFormat::Enum format_ = bgfx::TextureFormat::RGBA8;
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
    std::vector<std::uint16_t> halfPixels_;
    std::vector<std::uint8_t> bytePixels_;
};

class PresentationResources::FullscreenTexturePass {
public:
    ~FullscreenTexturePass() {
        shutdown();
    }

    bool initialise() {
        if (bgfx::isValid(program_) && bgfx::isValid(sampler_)) {
            return true;
        }

        shutdown();

        FullscreenVertex::initialiseLayout();

        const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
        const bgfx::ShaderHandle vertexShader =
            bgfx::createEmbeddedShader(kEmbeddedShaders, rendererType, "vs_ocornut_imgui");
        const bgfx::ShaderHandle fragmentShader =
            bgfx::createEmbeddedShader(kEmbeddedShaders, rendererType, "fs_ocornut_imgui");
        if (!bgfx::isValid(vertexShader) || !bgfx::isValid(fragmentShader)) {
            if (bgfx::isValid(vertexShader)) {
                bgfx::destroy(vertexShader);
            }
            if (bgfx::isValid(fragmentShader)) {
                bgfx::destroy(fragmentShader);
            }
            return false;
        }

        program_ = bgfx::createProgram(vertexShader, fragmentShader, true);
        sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
        return bgfx::isValid(program_) && bgfx::isValid(sampler_);
    }

    void shutdown() {
        if (bgfx::isValid(sampler_)) {
            bgfx::destroy(sampler_);
            sampler_ = BGFX_INVALID_HANDLE;
        }

        if (bgfx::isValid(program_)) {
            bgfx::destroy(program_);
            program_ = BGFX_INVALID_HANDLE;
        }
    }

    bool submit(const bgfx::ViewId viewId,
                const std::uint16_t width,
                const std::uint16_t height,
                const bgfx::TextureHandle texture) const {
        if (!bgfx::isValid(program_) || !bgfx::isValid(sampler_) || !bgfx::isValid(texture)) {
            return false;
        }

        if (bgfx::getAvailTransientVertexBuffer(6, FullscreenVertex::layout) != 6) {
            return false;
        }

        const bgfx::Caps* caps = bgfx::getCaps();
        float ortho[16];
        bx::mtxOrtho(ortho, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f,
                     0.0f, 1000.0f, 0.0f, caps != nullptr && caps->homogeneousDepth);

        bgfx::setViewName(viewId, "PresentationBackground");
        bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
        bgfx::setViewRect(viewId, 0, 0, width, height);
        bgfx::setViewTransform(viewId, nullptr, ortho);

        bgfx::TransientVertexBuffer vertexBuffer;
        bgfx::allocTransientVertexBuffer(&vertexBuffer, 6, FullscreenVertex::layout);
        auto* vertices = reinterpret_cast<FullscreenVertex*>(vertexBuffer.data);

        vertices[0] = FullscreenVertex{0.0f,                     0.0f,                      0.0f, 0.0f, UINT32_MAX};
        vertices[1] = FullscreenVertex{static_cast<float>(width), 0.0f,                      1.0f, 0.0f, UINT32_MAX};
        vertices[2] = FullscreenVertex{static_cast<float>(width), static_cast<float>(height), 1.0f, 1.0f, UINT32_MAX};
        vertices[3] = FullscreenVertex{static_cast<float>(width), static_cast<float>(height), 1.0f, 1.0f, UINT32_MAX};
        vertices[4] = FullscreenVertex{0.0f,                     static_cast<float>(height), 0.0f, 1.0f, UINT32_MAX};
        vertices[5] = FullscreenVertex{0.0f,                     0.0f,                      0.0f, 0.0f, UINT32_MAX};

        bgfx::Encoder* encoder = bgfx::begin();
        encoder->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        encoder->setTexture(0, sampler_, texture);
        encoder->setVertexBuffer(0, &vertexBuffer);
        encoder->submit(viewId, program_);
        bgfx::end(encoder);
        return true;
    }

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
};

PresentationResources::PresentationResources() = default;

PresentationResources::~PresentationResources() {
    shutdown();
}

bool PresentationResources::initialise() {
    shutdown();

    highPrecisionTexturesSupported_ =
        bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, 0);
    sampledTextureFormat_ = highPrecisionTexturesSupported_
        ? bgfx::TextureFormat::RGBA16F
        : bgfx::TextureFormat::RGBA8;

    sidebarColourTexture_ = std::make_unique<SampledTexture>("SidebarColourSurface");
    recorderColourTexture_ = std::make_unique<SampledTexture>("RecorderColourSurface");
    backgroundTexture_ = std::make_unique<SampledTexture>("BackgroundColourSurface");
    timelineTexture_ = std::make_unique<SampledTexture>("TimelineGradientSurface");
    backgroundPass_ = std::make_unique<FullscreenTexturePass>();

    initialised_ = true;
    backgroundPresentationSupported_ =
        backgroundPass_ != nullptr &&
        backgroundPass_->initialise();
    return true;
}

void PresentationResources::shutdown() {
    if (timelineTexture_ != nullptr) {
        timelineTexture_->shutdown();
    }
    if (sidebarColourTexture_ != nullptr) {
        sidebarColourTexture_->shutdown();
    }
    if (recorderColourTexture_ != nullptr) {
        recorderColourTexture_->shutdown();
    }
    if (backgroundTexture_ != nullptr) {
        backgroundTexture_->shutdown();
    }
    if (backgroundPass_ != nullptr) {
        backgroundPass_->shutdown();
    }

    timelineTexture_.reset();
    sidebarColourTexture_.reset();
    recorderColourTexture_.reset();
    backgroundTexture_.reset();
    backgroundPass_.reset();
    timelinePixels_.clear();
    timelineTextureCacheKey_ = {};
    backgroundPresentationSupported_ = false;
    initialised_ = false;
}

bool PresentationResources::supportsHighPrecisionTextures() const {
    return highPrecisionTexturesSupported_;
}

bool PresentationResources::supportsBackgroundPresentation() const {
    return backgroundPresentationSupported_;
}

ImTextureID PresentationResources::updateTimelineTexture(
    const std::vector<ReSyne::Timeline::TimelineSample>& samples,
    const uint64_t sampleRevision,
    const float visibleStart,
    const float visibleEnd,
    const int width,
    const ColourCore::ColourSpace colourSpace,
    const bool applyGamutMapping) {
    if (!initialised_ || timelineTexture_ == nullptr || samples.empty() || width <= 0) {
        return ImTextureID_Invalid;
    }

    const auto safeWidth = static_cast<std::uint16_t>(std::clamp(width, 1, static_cast<int>(std::numeric_limits<std::uint16_t>::max())));
    const bool canReuseCachedTexture =
        sampleRevision > 0 &&
        timelineTexture_->textureId() != ImTextureID_Invalid &&
        timelineTextureCacheKey_.valid &&
        timelineTextureCacheKey_.sampleRevision == sampleRevision &&
        timelineTextureCacheKey_.width == safeWidth &&
        timelineTextureCacheKey_.visibleStart == visibleStart &&
        timelineTextureCacheKey_.visibleEnd == visibleEnd &&
        timelineTextureCacheKey_.colourSpace == colourSpace &&
        timelineTextureCacheKey_.applyGamutMapping == applyGamutMapping;
    if (canReuseCachedTexture) {
        return timelineTexture_->textureId();
    }

    timelinePixels_.resize(static_cast<std::size_t>(safeWidth) * 4);
    ReSyne::Timeline::rasteriseGradientStrip(
        samples,
        visibleStart,
        visibleEnd,
        safeWidth,
        colourSpace,
        applyGamutMapping,
        timelinePixels_);

    if (!timelineTexture_->update(timelinePixels_, safeWidth, 1, sampledTextureFormat_)) {
        return ImTextureID_Invalid;
    }

    timelineTextureCacheKey_ = TimelineTextureCacheKey{
        true,
        sampleRevision,
        safeWidth,
        visibleStart,
        visibleEnd,
        colourSpace,
        applyGamutMapping
    };

    return timelineTexture_->textureId();
}

ImTextureID PresentationResources::updateActiveColourTexture(
    const PreviewSurface surface,
    float r,
    float g,
    float b) {
    SampledTexture* texture = nullptr;
    switch (surface) {
        case PreviewSurface::Sidebar:
            texture = sidebarColourTexture_.get();
            break;
        case PreviewSurface::Recorder:
            texture = recorderColourTexture_.get();
            break;
    }

    if (!initialised_ || texture == nullptr) {
        return ImTextureID_Invalid;
    }

    ColourPresentation::applyOutputPrecision(r, g, b);
    solidPixel_[0] = r;
    solidPixel_[1] = g;
    solidPixel_[2] = b;
    solidPixel_[3] = 1.0f;

    if (!texture->update(solidPixel_, 1, 1, sampledTextureFormat_)) {
        return ImTextureID_Invalid;
    }

    return texture->textureId();
}

bool PresentationResources::submitBackground(const bgfx::ViewId viewId,
                                             const std::uint16_t width,
                                             const std::uint16_t height,
                                             float r,
                                             float g,
                                             float b) const {
    if (!initialised_ ||
        !backgroundPresentationSupported_ ||
        backgroundTexture_ == nullptr ||
        backgroundPass_ == nullptr) {
        return false;
    }

    ColourPresentation::applyOutputPrecision(r, g, b);

    auto solidPixel = solidPixel_;
    solidPixel[0] = r;
    solidPixel[1] = g;
    solidPixel[2] = b;
    solidPixel[3] = 1.0f;

    if (!backgroundTexture_->update(solidPixel, 1, 1, sampledTextureFormat_)) {
        return false;
    }

    return backgroundPass_->submit(viewId, width, height, backgroundTexture_->handle());
}

}

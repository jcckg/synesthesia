#pragma once

#include <array>
#include <memory>
#include <vector>

#include <bgfx/bgfx.h>

#include "imgui.h"

#include "colour/colour_core.h"
#include "resyne/ui/timeline/timeline.h"

namespace Renderer {

class PresentationResources {
public:
    enum class PreviewSurface {
        Sidebar,
        Recorder
    };

    PresentationResources();
    ~PresentationResources();

    PresentationResources(const PresentationResources&) = delete;
    PresentationResources& operator=(const PresentationResources&) = delete;

    bool initialise();
    void shutdown();

    [[nodiscard]] bool supportsHighPrecisionTextures() const;
    [[nodiscard]] bool supportsBackgroundPresentation() const;

    [[nodiscard]] ImTextureID updateTimelineTexture(const std::vector<ReSyne::Timeline::TimelineSample>& samples,
                                                    float visibleStart,
                                                    float visibleEnd,
                                                    int width,
                                                    ColourCore::ColourSpace colourSpace,
                                                    bool applyGamutMapping);

    [[nodiscard]] ImTextureID updateActiveColourTexture(PreviewSurface surface,
                                                        float r,
                                                        float g,
                                                        float b);

    bool submitBackground(bgfx::ViewId viewId,
                          uint16_t width,
                          uint16_t height,
                          float r,
                          float g,
                          float b) const;

private:
    class SampledTexture;
    class FullscreenTexturePass;

    bgfx::TextureFormat::Enum sampledTextureFormat_ = bgfx::TextureFormat::RGBA8;
    bool highPrecisionTexturesSupported_ = false;
    bool backgroundPresentationSupported_ = false;
    bool initialised_ = false;

    std::vector<float> timelinePixels_;
    std::array<float, 4> solidPixel_ = {0.0f, 0.0f, 0.0f, 1.0f};

    std::unique_ptr<SampledTexture> sidebarColourTexture_;
    std::unique_ptr<SampledTexture> recorderColourTexture_;
    std::unique_ptr<SampledTexture> backgroundTexture_;
    std::unique_ptr<SampledTexture> timelineTexture_;
    std::unique_ptr<FullscreenTexturePass> backgroundPass_;
};

}

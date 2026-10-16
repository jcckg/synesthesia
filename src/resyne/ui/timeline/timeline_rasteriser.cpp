#include "resyne/ui/timeline/timeline_rasteriser.h"

#include <algorithm>
#include <cmath>

#include "colour/colour_presentation.h"

namespace ReSyne::Timeline {

std::size_t gradientPixelWidth(float logicalWidth, float framebufferScale, std::size_t limit) {
    if (!std::isfinite(logicalWidth) || !std::isfinite(framebufferScale) || limit == 0) return 0;
    return static_cast<std::size_t>(std::clamp(std::ceil(static_cast<double>(logicalWidth) *
        std::max(1.0f, framebufferScale)), 1.0, static_cast<double>(limit)));
}

// https://www.pbr-book.org/4ed/Sampling_and_Reconstruction/Sampling_Theory
void rasteriseGradientStrip(std::span<const TimelineSample> samples,
                            float visibleStart, float visibleEnd, std::size_t width,
                            ColourCore::ColourSpace colourSpace, bool applyGamutMapping,
                            std::span<float> rgbaPixels,
                            const ColourPresentation::ColourTrack* track, double duration) {
    (void)colourSpace;
    if (width == 0 || rgbaPixels.size() / 4 < width) return;
    (void)samples;
    if (track == nullptr || track->size() == 0) {
        std::fill_n(rgbaPixels.begin(), width * 4, 0.0f);
        return;
    }
    visibleStart = std::isfinite(visibleStart) ? std::clamp(visibleStart, 0.0f, 1.0f) : 0.0f;
    visibleEnd = std::isfinite(visibleEnd) ? std::clamp(visibleEnd, visibleStart, 1.0f) : visibleStart;
    if (!std::isfinite(duration) || duration <= 0.0) duration = track->endTime();
    const double start = visibleStart * duration;
    const double step = (visibleEnd - visibleStart) * duration / width;
    for (std::size_t pixel = 0; pixel < width; ++pixel) {
        auto rgb = track->averageRGB(start + pixel * step, start + (pixel + 1) * step,
                                    ColourCore::ColourSpace::SRGB, applyGamutMapping);
        ColourPresentation::applyOutputPrecision(rgb.r, rgb.g, rgb.b);
        rgbaPixels[pixel * 4] = rgb.r;
        rgbaPixels[pixel * 4 + 1] = rgb.g;
        rgbaPixels[pixel * 4 + 2] = rgb.b;
        rgbaPixels[pixel * 4 + 3] = 1.0f;
    }
}

}

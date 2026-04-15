#include "resyne/ui/timeline/timeline_rasteriser.h"

#include <algorithm>
#include <cmath>

#include "colour/colour_presentation.h"

namespace ReSyne::Timeline {

namespace {

void writePixel(std::span<float> rgbaPixels,
                const std::size_t pixelIndex,
                float r,
                float g,
                float b) {
    ColourPresentation::applyOutputPrecision(r, g, b);

    const std::size_t baseIndex = pixelIndex * 4;
    rgbaPixels[baseIndex + 0] = r;
    rgbaPixels[baseIndex + 1] = g;
    rgbaPixels[baseIndex + 2] = b;
    rgbaPixels[baseIndex + 3] = 1.0f;
}

}

void rasteriseGradientStrip(const std::span<const TimelineSample> samples,
                            float visibleStart,
                            float visibleEnd,
                            const std::size_t width,
                            const ColourCore::ColourSpace colourSpace,
                            const bool applyGamutMapping,
                            const std::span<float> rgbaPixels) {
    if (width == 0 || rgbaPixels.size() < width * 4) {
        return;
    }

    visibleStart = std::clamp(visibleStart, 0.0f, 1.0f);
    visibleEnd = std::clamp(visibleEnd, visibleStart, 1.0f);

    if (samples.empty()) {
        std::fill(rgbaPixels.begin(), rgbaPixels.begin() + static_cast<std::ptrdiff_t>(width * 4), 0.0f);
        return;
    }

    const std::size_t lastIndex = samples.size() - 1;
    for (std::size_t pixelIndex = 0; pixelIndex < width; ++pixelIndex) {
        const float pixelNormalised = width > 1
            ? static_cast<float>(pixelIndex) / static_cast<float>(width - 1)
            : 0.0f;
        const float visiblePosition = std::lerp(visibleStart, visibleEnd, pixelNormalised);
        const float samplePosition = visiblePosition * static_cast<float>(lastIndex);
        const std::size_t sampleIndex0 = std::min(static_cast<std::size_t>(samplePosition), lastIndex);
        const std::size_t sampleIndex1 = std::min(sampleIndex0 + 1, lastIndex);
        const float fraction = samplePosition - static_cast<float>(sampleIndex0);

        const auto& sample0 = samples[sampleIndex0];
        const auto& sample1 = samples[sampleIndex1];

        const float L = std::lerp(sample0.labL, sample1.labL, fraction);
        const float a = std::lerp(sample0.labA, sample1.labA, fraction);
        const float bComponent = std::lerp(sample0.labB, sample1.labB, fraction);

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        ColourCore::LabtoRGB(L, a, bComponent, r, g, b, colourSpace, applyGamutMapping);
        writePixel(rgbaPixels, pixelIndex, r, g, b);
    }
}

}

#pragma once

#include <cstddef>
#include <span>

#include "colour/colour_core.h"
#include "colour/colour_track.h"
#include "resyne/ui/timeline/timeline.h"

namespace ReSyne::Timeline {

void rasteriseGradientStrip(std::span<const TimelineSample> samples,
                            float visibleStart,
                            float visibleEnd,
                            std::size_t width,
                            ColourCore::ColourSpace colourSpace,
                            bool applyGamutMapping,
                            std::span<float> rgbaPixels,
                            const ColourPresentation::ColourTrack* track = nullptr,
                            double duration = 0.0);

std::size_t gradientPixelWidth(float logicalWidth, float framebufferScale, std::size_t limit = 65535);

}

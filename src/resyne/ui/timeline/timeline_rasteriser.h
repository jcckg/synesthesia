#pragma once

#include <cstddef>
#include <span>

#include "colour/colour_core.h"
#include "resyne/ui/timeline/timeline.h"

namespace ReSyne::Timeline {

void rasteriseGradientStrip(std::span<const TimelineSample> samples,
                            float visibleStart,
                            float visibleEnd,
                            std::size_t width,
                            ColourCore::ColourSpace colourSpace,
                            bool applyGamutMapping,
                            std::span<float> rgbaPixels);

}

#pragma once

#include <imgui.h>
#include <vector>
#include "colour/colour_core.h"
#include "resyne/ui/timeline/timeline.h"

namespace ReSyne::Timeline::Gradient {

ImVec4 interpolateColour(const TimelineSample& a,
                         const TimelineSample& b,
                         float t,
                         ColourCore::ColourSpace colourSpace,
                         bool applyGamutMapping = true);

void drawGradient(ImDrawList* drawList,
                  const ImVec2& min,
                  const ImVec2& max,
                  const std::vector<TimelineSample>& samples,
                  float visibleStart,
                  float visibleEnd,
                  ColourCore::ColourSpace colourSpace,
                  bool applyGamutMapping = true);

}

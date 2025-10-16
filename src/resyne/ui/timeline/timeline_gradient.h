#pragma once

#include <imgui.h>
#include <vector>
#include "colour/colour_mapper.h"
#include "resyne/ui/timeline/timeline.h"

namespace ReSyne::Timeline::Gradient {

ImVec4 interpolateColour(const TimelineSample& a,
                         const TimelineSample& b,
                         float t,
                         ColourMapper::ColourSpace colourSpace);

void drawGradient(ImDrawList* drawList,
                  const ImVec2& min,
                  const ImVec2& max,
                  const std::vector<TimelineSample>& samples,
                  float visibleStart,
                  float visibleEnd,
                  ColourMapper::ColourSpace colourSpace);

}

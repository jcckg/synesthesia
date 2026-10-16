#include "timeline_gradient.h"

#include <algorithm>
#include "imgui_internal.h"
#include <vector>

#include "colour/colour_presentation.h"
#include "resyne/ui/timeline/timeline_rasteriser.h"

namespace ReSyne::Timeline::Gradient {

void drawGradient(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
                  const std::vector<TimelineSample>& samples, float visibleStart, float visibleEnd,
                  ColourCore::ColourSpace colourSpace, bool applyGamutMapping,
                  const ColourPresentation::ColourTrack* track, double duration) {
    if (drawList == nullptr || max.x <= min.x || max.y <= min.y) return;
    const auto width = gradientPixelWidth(max.x - min.x, ImGui::GetWindowViewport()->FramebufferScale.x);
    if (width == 0) return;
    std::vector<float> pixels(width * 4);
    rasteriseGradientStrip(samples, visibleStart, visibleEnd, width, colourSpace,
                           applyGamutMapping, pixels, track, duration);
    for (std::size_t i = 0; i < width; ++i) {
        const auto colour = ImGui::ColorConvertFloat4ToU32({pixels[i*4], pixels[i*4+1], pixels[i*4+2], pixels[i*4+3]});
        const float x0 = std::lerp(min.x, max.x, static_cast<float>(i) / width);
        const float x1 = std::lerp(min.x, max.x, static_cast<float>(i + 1) / width);
        drawList->AddRectFilled({x0, min.y}, {x1, max.y}, colour);
    }
}

}

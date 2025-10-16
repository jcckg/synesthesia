#include "timeline.h"

#include <algorithm>

#include "timeline_render.h"
#include "timeline_gradient.h"
#include "resyne/encoding/formats/exporter.h"
#include "colour/colour_mapper.h"

namespace ReSyne::Timeline {

RenderResult renderTimeline(TimelineState& state, const RenderContext& context) {
    return Render::renderTimelineImpl(state, context);
}

ImVec4 getColourAt(const std::vector<TimelineSample>& samples,
                   float normalisedPosition,
                   ColourMapper::ColourSpace colourSpace) {
    if (samples.empty()) {
        return ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    if (samples.size() == 1) {
        return samples[0].colour;
    }

    const float position = std::clamp(normalisedPosition, 0.0f, 1.0f) * (static_cast<float>(samples.size()) - 1.0f);
    const size_t index1 = static_cast<size_t>(position);
    const size_t index2 = std::min(index1 + 1, samples.size() - 1);
    const float t = position - static_cast<float>(index1);

    return Gradient::interpolateColour(samples[index1], samples[index2], t, colourSpace);
}

}

#include "timeline.h"

#include <algorithm>

#include "timeline_render.h"
#include "timeline_gradient.h"
#include "resyne/encoding/formats/exporter.h"
#include "colour/colour_core.h"

namespace ReSyne::Timeline {

RenderResult renderTimeline(TimelineState& state, const RenderContext& context) {
    return Render::renderTimelineImpl(state, context);
}

}

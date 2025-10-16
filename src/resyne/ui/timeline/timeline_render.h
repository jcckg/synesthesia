#pragma once

#include "timeline.h"

namespace ReSyne::Timeline::Render {

UI::Utilities::ToolType resolveTool(const RenderContext& context,
                                     bool commandLikeDown,
                                     bool altDown);

RenderResult renderTimelineImpl(TimelineState& state, const RenderContext& context);

}

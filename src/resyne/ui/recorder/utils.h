#pragma once

#include <optional>

#include "resyne/recorder/recorder.h"
#include "resyne/ui/timeline/timeline.h"

namespace ReSyne::RecorderUI {

std::optional<float> computePlaybackNormalisedPosition(const RecorderState& state);
void handleTimelineScrub(RecorderState& state,
                         const Timeline::RenderResult& result);

}

#pragma once

#include <vector>
#include <mutex>
#include "resyne/recorder/recorder.h"
#include "resyne/ui/timeline/timeline.h"

namespace ReSyne::UI {

std::vector<Timeline::TimelineSample> samplePreviewData(
    RecorderState& state,
    size_t maxSamples,
    std::lock_guard<std::mutex>& lock
);

void renderStatusMessage(RecorderState& state);

}

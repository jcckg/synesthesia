#pragma once

#include <vector>
#include <mutex>
#include "resyne/recorder/recorder.h"
#include "resyne/ui/timeline/timeline.h"

namespace ReSyne {
struct PresentationSnapshot;
}

namespace ReSyne::UI {

ImVec4 loadingStatusColour();
void renderStatusMessage(RecorderState& state);
ImVec4 activityStatusColour(RecorderState& state, const AudioInput& audioInput, const PresentationSnapshot* presentation = nullptr);

}

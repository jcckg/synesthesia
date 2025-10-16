#pragma once

#include "resyne/recorder/recorder.h"

namespace ReSyne {

struct State {
    RecorderState recorderState;
    float displayColour[3] = {0.0f, 0.0f, 0.0f};
};

}

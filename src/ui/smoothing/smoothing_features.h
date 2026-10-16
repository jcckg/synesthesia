#pragma once

#include "colour/colour_core.h"
#include "ui/smoothing/smoothing.h"

namespace UI::Smoothing {
SmoothingSignalFeatures buildSignalFeatures(const ColourCore::FrameResult& result);
}

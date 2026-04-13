#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "colour/colour_core.h"
#include "ui/smoothing/smoothing.h"

namespace UI::Smoothing {

struct MagnitudeHistory {
    std::vector<float> previousMagnitudes;
    std::array<float, 12> fluxHistory{};
    std::size_t fluxHistoryIndex = 0;
};

SmoothingSignalFeatures buildSignalFeatures(const ColourCore::FrameResult& result);

void updateFluxHistory(const std::vector<float>& visualiserMagnitudes,
                       MagnitudeHistory& history,
                       SmoothingSignalFeatures& features);

}

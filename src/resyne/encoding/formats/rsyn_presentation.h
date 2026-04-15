#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "resyne/encoding/formats/exporter.h"

namespace RSYNPresentation {

std::shared_ptr<RSYNPresentationData> buildPresentationData(
    const std::vector<AudioColourSample>& samples,
    const RSYNPresentationSettings& settings,
    const std::function<void(float)>& progress = {});

}

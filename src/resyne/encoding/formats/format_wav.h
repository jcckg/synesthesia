#pragma once

#include <string>
#include <vector>
#include <functional>
#include "resyne/encoding/formats/exporter.h"

namespace SequenceExporterInternal {

bool exportToWAV(const std::string& filepath,
                const std::vector<AudioColourSample>& samples,
                const AudioMetadata& metadata,
                const std::function<void(float)>& progress = {});

}

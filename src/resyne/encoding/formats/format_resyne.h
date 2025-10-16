#pragma once

#include <string>
#include <vector>
#include <functional>
#include "resyne/encoding/formats/exporter.h"

namespace SequenceExporterInternal {

bool exportToResyne(const std::string& filepath,
                   const std::vector<AudioColourSample>& samples,
                   const AudioMetadata& metadata,
                   const std::function<void(float)>& progress = {});

bool loadFromResyne(const std::string& filepath,
                    std::vector<AudioColourSample>& samples,
                    AudioMetadata& metadata,
                    const std::function<void(float)>& progress = {},
                    const SequenceFrameCallback& onFrameDecoded = {});

}

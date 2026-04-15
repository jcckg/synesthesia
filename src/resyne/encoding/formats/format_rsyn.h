#pragma once

#include <functional>
#include <string>
#include <vector>

#include "resyne/encoding/formats/exporter.h"

namespace SequenceExporterInternal {

bool exportToRsyn(const std::string& filepath,
                  const std::vector<AudioColourSample>& samples,
                  const AudioMetadata& metadata,
                  const RSYNExportOptions& options,
                  const std::function<void(float)>& progress = {});

bool loadFromRsyn(const std::string& filepath,
                  std::vector<AudioColourSample>& samples,
                  AudioMetadata& metadata,
                  const std::function<void(float)>& progress = {},
                  const SequenceFrameCallback& onFrameDecoded = {});

bool loadFromRsynShell(const std::string& filepath,
                       AudioMetadata& metadata,
                       const std::function<void(float)>& progress = {});

bool hydrateRsynSamples(AudioMetadata& metadata,
                        std::vector<AudioColourSample>& samples,
                        const std::function<void(float)>& progress = {},
                        const SequenceFrameCallback& onFrameDecoded = {});

bool hydrateRsynSource(AudioMetadata& metadata,
                       const std::function<void(float)>& progress = {});

}

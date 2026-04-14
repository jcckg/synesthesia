#pragma once

#include <functional>
#include <vector>

#include "resyne/encoding/formats/exporter.h"

namespace ReSyne::RecorderReconstruction {

using ProgressCallback = std::function<void(float)>;

bool buildPlaybackAudio(const std::vector<AudioColourSample>& samples,
                        const AudioMetadata& metadata,
                        std::vector<float>& playbackAudio,
                        const ProgressCallback& onProgress = nullptr);

}

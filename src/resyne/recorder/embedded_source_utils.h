#pragma once

#include <string>
#include <vector>

#include "resyne/encoding/formats/exporter.h"

namespace ReSyne::EmbeddedSourceUtils {

bool decodeEmbeddedSourceAudio(const AudioMetadata& metadata,
                               std::vector<float>& playbackAudio,
                               std::string& errorMessage);

}

#pragma once

#include <vector>

#include "resyne/encoding/formats/exporter.h"

namespace ReSyne::LoudnessUtils {


void calculateLoudnessFromSpectralFrames(std::vector<AudioColourSample>& samples,
										  const AudioMetadata& metadata, bool replaceExisting = false);

}

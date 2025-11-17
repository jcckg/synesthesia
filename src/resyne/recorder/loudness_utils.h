#pragma once

#include <vector>

#include "resyne/encoding/formats/exporter.h"

namespace ReSyne::LoudnessUtils {

// ITU-R BS.1770-4 compliant loudness calculation from spectral frames
// Reconstructs time-domain audio via IFFT and processes through K-weighted loudness meter
void calculateLoudnessFromSpectralFrames(std::vector<AudioColourSample>& samples,
										  const AudioMetadata& metadata);

}

#pragma once

#include <string>
#include "resyne/decoding/audio_decoder.h"

namespace AudioDecoding {

bool decodeWav(const std::string& filepath, DecodedAudio& out, std::string& error);

}

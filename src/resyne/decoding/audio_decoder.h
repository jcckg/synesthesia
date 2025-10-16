#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace AudioDecoding {

struct DecodedAudio {
    std::vector<float> samples;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
};

bool decodeFile(const std::string& filepath, DecodedAudio& out, std::string& errorMessage);

}


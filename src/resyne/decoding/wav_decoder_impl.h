#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace WAVDecoder {

struct DecodedWAV {
    std::vector<std::vector<float>> channelSamples;
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
};

bool decodeFile(const std::string& filepath, DecodedWAV& out, std::string& errorMessage);

}

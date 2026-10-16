#pragma once

#include <cstdint>
#include <string>

namespace SequenceExporterInternal::TiffMetadata {

struct AudioFields {
    std::uint32_t sampleRate = 0;
    std::uint32_t fftSize = 0;
    std::uint32_t hopSize = 0;
    std::uint32_t channels = 0;
    std::uint32_t analysisLeadingSamples = 0;
    std::uint32_t originalSampleCountLow = 0;
    std::uint32_t originalSampleCountHigh = 0;
};

AudioFields read(const std::string& filepath);
bool write(const std::string& filepath, const AudioFields& metadata);

}

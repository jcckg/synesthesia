#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "resyne/encoding/formats/exporter.h"

namespace RSYNSerialisation {

bool encodeMetadata(const AudioMetadata& metadata, std::vector<std::uint8_t>& output);
bool decodeMetadata(const std::vector<std::uint8_t>& input, AudioMetadata& metadata);

bool encodeSourceBytes(const std::shared_ptr<RSYNSourceData>& sourceData,
                       std::vector<std::uint8_t>& output);
bool decodeSourceBytes(const std::vector<std::uint8_t>& input,
                       AudioMetadata& metadata);

bool encodeSamples(const std::vector<AudioColourSample>& samples,
                   std::vector<std::uint8_t>& output);
bool decodeSamples(const std::vector<std::uint8_t>& input,
                   std::vector<AudioColourSample>& samples,
                   const SequenceFrameCallback& onFrameDecoded = {},
                   const std::function<void(float)>& progress = {});

bool encodePresentationFrames(const std::shared_ptr<RSYNPresentationData>& presentationData,
                              std::vector<std::uint8_t>& output);
bool decodePresentationFrames(const std::vector<std::uint8_t>& input,
                              AudioMetadata& metadata,
                              const std::function<void(float)>& progress = {});

}

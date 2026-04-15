#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace RSYNContainer {

enum class Compression : std::uint32_t {
    None = 0,
    Deflate = 1
};

struct Chunk {
    std::uint32_t tag = 0;
    std::vector<std::uint8_t> payload;
};

struct ChunkLocator {
    std::uint32_t tag = 0;
    Compression compression = Compression::None;
    std::uint64_t offset = 0;
    std::uint64_t storedSize = 0;
    std::uint64_t unpackedSize = 0;
    std::uint32_t crc32 = 0;
};

using ChunkMap = std::unordered_map<std::uint32_t, std::vector<std::uint8_t>>;
using ChunkIndex = std::unordered_map<std::uint32_t, ChunkLocator>;

constexpr std::uint32_t makeTag(const char (&text)[5]) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[0])) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[1])) << 8U) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[2])) << 16U) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[3])) << 24U);
}

bool writeFile(const std::string& filepath,
               const std::vector<Chunk>& chunks,
               const std::function<void(float)>& progress = {});

bool readIndex(const std::string& filepath,
               ChunkIndex& index,
               const std::function<void(float)>& progress = {});

bool readChunk(const std::string& filepath,
               const ChunkLocator& locator,
               std::vector<std::uint8_t>& payload);

bool readFile(const std::string& filepath,
              ChunkMap& chunks,
              const std::function<void(float)>& progress = {});

}

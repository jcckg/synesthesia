#include "utilities/io.h"
#include "resyne/encoding/formats/rsyn_container.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#include <type_traits>

#include "miniz.h"
#undef crc32

namespace RSYNContainer {

namespace {

constexpr std::array<char, 4> kMagic = {'R', 'S', 'Y', '2'};
constexpr std::uint32_t kVersion = 2;

struct Header {
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint64_t tocOffset = 0;
    std::uint32_t tocCount = 0;
    std::uint32_t reserved = 0;
};

struct TocEntry {
    std::uint32_t tag = 0;
    std::uint32_t compression = 0;
    std::uint64_t offset = 0;
    std::uint64_t storedSize = 0;
    std::uint64_t unpackedSize = 0;
    std::uint32_t crc32 = 0;
    std::uint32_t reserved = 0;
};

template <typename T, bool IsEnum = std::is_enum_v<T>>
struct UnsignedStorage;

template <typename T>
struct UnsignedStorage<T, false> {
    using type = std::make_unsigned_t<T>;
};

template <typename T>
struct UnsignedStorage<T, true> {
    using type = std::make_unsigned_t<std::underlying_type_t<T>>;
};

template <typename T>
void appendLittleEndian(std::vector<std::uint8_t>& buffer, T value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);

    using Unsigned = typename UnsignedStorage<T>::type;
    const Unsigned encoded = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        buffer.push_back(static_cast<std::uint8_t>((encoded >> (index * 8U)) & 0xFFU));
    }
}

template <typename T>
bool readLittleEndian(const std::vector<std::uint8_t>& buffer, std::size_t& offset, T& value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);

    if (offset + sizeof(T) > buffer.size()) {
        return false;
    }

    using Unsigned = typename UnsignedStorage<T>::type;
    Unsigned decoded = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        decoded |= static_cast<Unsigned>(buffer[offset + index]) << (index * 8U);
    }
    value = static_cast<T>(decoded);
    offset += sizeof(T);
    return true;
}

std::vector<std::uint8_t> encodeHeader(const Header& header) {
    std::vector<std::uint8_t> buffer;
    buffer.reserve(24);
    buffer.insert(buffer.end(), header.magic.begin(), header.magic.end());
    appendLittleEndian(buffer, header.version);
    appendLittleEndian(buffer, header.tocOffset);
    appendLittleEndian(buffer, header.tocCount);
    appendLittleEndian(buffer, header.reserved);
    return buffer;
}

bool decodeHeader(const std::vector<std::uint8_t>& buffer, Header& header) {
    if (buffer.size() < 24) {
        return false;
    }

    std::copy_n(buffer.begin(), header.magic.size(), header.magic.begin());
    std::size_t offset = header.magic.size();
    return readLittleEndian(buffer, offset, header.version) &&
        readLittleEndian(buffer, offset, header.tocOffset) &&
        readLittleEndian(buffer, offset, header.tocCount) &&
        readLittleEndian(buffer, offset, header.reserved);
}

std::vector<std::uint8_t> encodeTocEntry(const TocEntry& entry) {
    std::vector<std::uint8_t> buffer;
    buffer.reserve(40);
    appendLittleEndian(buffer, entry.tag);
    appendLittleEndian(buffer, entry.compression);
    appendLittleEndian(buffer, entry.offset);
    appendLittleEndian(buffer, entry.storedSize);
    appendLittleEndian(buffer, entry.unpackedSize);
    appendLittleEndian(buffer, entry.crc32);
    appendLittleEndian(buffer, entry.reserved);
    return buffer;
}

bool decodeTocEntry(const std::vector<std::uint8_t>& buffer, std::size_t& offset, TocEntry& entry) {
    return readLittleEndian(buffer, offset, entry.tag) &&
        readLittleEndian(buffer, offset, entry.compression) &&
        readLittleEndian(buffer, offset, entry.offset) &&
        readLittleEndian(buffer, offset, entry.storedSize) &&
        readLittleEndian(buffer, offset, entry.unpackedSize) &&
        readLittleEndian(buffer, offset, entry.crc32) &&
        readLittleEndian(buffer, offset, entry.reserved);
}

std::uint32_t crc32For(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return 0;
    }
    mz_ulong value = MZ_CRC32_INIT;
    for (std::size_t offset = 0; offset < data.size();) {
        Utilities::checkCancellation();
        const auto count = std::min<std::size_t>(data.size() - offset, 1024 * 1024);
        value = mz_crc32(value, data.data() + offset, count);
        offset += count;
    }
    return static_cast<std::uint32_t>(value);
}

bool compressPayload(const std::vector<std::uint8_t>& input,
                     const int compressionLevel,
                     Compression& compression,
                     std::vector<std::uint8_t>& output) {
    if (input.empty()) {
        compression = Compression::None;
        output.clear();
        return true;
    }

    mz_stream stream{};
    if (mz_deflateInit(&stream, compressionLevel) != MZ_OK) return false;
    const auto end = [](mz_stream* value) { mz_deflateEnd(value); };
    std::unique_ptr<mz_stream, decltype(end)> guard(&stream, end);
    std::vector<std::uint8_t> compressed;
    std::array<std::uint8_t, 65536> block;
    std::size_t offset = 0;
    int status = MZ_OK;
    while (status != MZ_STREAM_END) {
        Utilities::checkCancellation();
        if (stream.avail_in == 0 && offset < input.size()) {
            const auto count = std::min<std::size_t>(input.size() - offset, 1024 * 1024);
            stream.next_in = input.data() + offset;
            stream.avail_in = static_cast<unsigned int>(count);
            offset += count;
        }
        stream.next_out = block.data();
        stream.avail_out = static_cast<unsigned int>(block.size());
        status = mz_deflate(&stream, offset == input.size() ? MZ_FINISH : MZ_NO_FLUSH);
        if (status != MZ_OK && status != MZ_STREAM_END) return false;
        compressed.insert(compressed.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(block.size() - stream.avail_out));
    }
    if (compressed.size() >= input.size()) {
        compression = Compression::None;
        output.clear();
    } else {
        compression = Compression::Deflate;
        output = std::move(compressed);
    }
    return true;
}

bool decompressPayload(const Compression compression,
                       const std::vector<std::uint8_t>& input,
                       const std::size_t expectedSize,
                       std::vector<std::uint8_t>& output) {
    if (compression == Compression::None) {
        output = input;
        return true;
    }

    if (compression != Compression::Deflate) {
        return false;
    }

    output.resize(expectedSize);
    mz_stream stream{};
    if (mz_inflateInit(&stream) != MZ_OK) return false;
    const auto end = [](mz_stream* value) { mz_inflateEnd(value); };
    std::unique_ptr<mz_stream, decltype(end)> guard(&stream, end);
    std::size_t inputOffset = 0, outputOffset = 0;
    int status = MZ_OK;
    while (status != MZ_STREAM_END) {
        Utilities::checkCancellation();
        if (stream.avail_in == 0 && inputOffset < input.size()) {
            const auto count = std::min<std::size_t>(input.size() - inputOffset, 1024 * 1024);
            stream.next_in = input.data() + inputOffset;
            stream.avail_in = static_cast<unsigned int>(count);
            inputOffset += count;
        }
        if (stream.avail_out == 0 && outputOffset < output.size()) {
            const auto count = std::min<std::size_t>(output.size() - outputOffset, 1024 * 1024);
            stream.next_out = output.data() + outputOffset;
            stream.avail_out = static_cast<unsigned int>(count);
            outputOffset += count;
        }
        const auto before = stream.total_out;
        const auto consumed = stream.total_in;
        status = mz_inflate(&stream, MZ_NO_FLUSH);
        if (status != MZ_OK && status != MZ_STREAM_END) return false;
        if (status != MZ_STREAM_END && before == stream.total_out && consumed == stream.total_in) return false;
    }
    return stream.total_out == expectedSize && stream.total_in == input.size();
}

std::vector<std::uint8_t> readBytes(std::ifstream& file, const std::uint64_t offset, const std::uint64_t size) {
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return {};
    }

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file.good()) {
        return {};
    }

    if (!buffer.empty()) {
        if (!Utilities::readBytes(file, buffer.data(), buffer.size())) {
            return {};
        }
    }

    return buffer;
}

void emitProgress(const std::function<void(float)>& progress, const float value) {
    if (!progress) {
        return;
    }
    progress(std::clamp(value, 0.0f, 1.0f));
}

}

template <typename Source>
bool writeChunks(const std::string& filepath,
               const std::vector<Source>& chunks,
               const int compressionLevel,
               const std::function<void(float)>& progress) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    Header header{};
    header.magic = kMagic;
    header.version = kVersion;

    const auto encodedHeader = encodeHeader(header);
    file.write(reinterpret_cast<const char*>(encodedHeader.data()), static_cast<std::streamsize>(encodedHeader.size()));
    if (!file.good()) {
        return false;
    }

    std::vector<TocEntry> tocEntries;
    tocEntries.reserve(chunks.size());

    for (std::size_t index = 0; index < chunks.size(); ++index) {
        Utilities::checkCancellation();
        std::vector<std::uint8_t> generated;
        const std::vector<std::uint8_t>* payload;
        if constexpr (std::is_same_v<Source, Chunk>) payload = &chunks[index].payload;
        else {
            if (!chunks[index].build(generated)) return false;
            payload = &generated;
        }
        Compression compression = Compression::None;
        std::vector<std::uint8_t> compressedPayload;
        const std::vector<std::uint8_t>* storedPayload = payload;
        if (compressionLevel > 0 && chunks[index].allowCompression) {
            if (!compressPayload(*payload, compressionLevel, compression, compressedPayload)) {
                return false;
            }
            if (compression != Compression::None) storedPayload = &compressedPayload;
        }

        const std::uint64_t payloadOffset = static_cast<std::uint64_t>(file.tellp());
        if (!storedPayload->empty()) {
            if (!Utilities::writeBytes(file, storedPayload->data(), storedPayload->size())) {
                return false;
            }
        }

        TocEntry entry{};
        entry.tag = chunks[index].tag;
        entry.compression = static_cast<std::uint32_t>(compression);
        entry.offset = payloadOffset;
        entry.storedSize = storedPayload->size();
        entry.unpackedSize = payload->size();
        entry.crc32 = crc32For(*payload);
        tocEntries.push_back(entry);

        emitProgress(progress, static_cast<float>(index + 1) / static_cast<float>(std::max<std::size_t>(1, chunks.size())) * 0.8f);
    }

    header.tocOffset = static_cast<std::uint64_t>(file.tellp());
    header.tocCount = static_cast<std::uint32_t>(tocEntries.size());
    for (const TocEntry& entry : tocEntries) {
        const auto encodedEntry = encodeTocEntry(entry);
        file.write(reinterpret_cast<const char*>(encodedEntry.data()), static_cast<std::streamsize>(encodedEntry.size()));
        if (!file.good()) {
            return false;
        }
    }

    file.seekp(0, std::ios::beg);
    const auto finalHeader = encodeHeader(header);
    file.write(reinterpret_cast<const char*>(finalHeader.data()), static_cast<std::streamsize>(finalHeader.size()));
    if (!file.good()) {
        return false;
    }

    emitProgress(progress, 1.0f);
    return true;
}

bool writeFile(const std::string& path, const std::vector<Chunk>& chunks, int level, const std::function<void(float)>& progress) {
    return writeChunks(path, chunks, level, progress);
}

bool writeFile(const std::string& path, const std::vector<ChunkSource>& chunks, int level, const std::function<void(float)>& progress) {
    return writeChunks(path, chunks, level, progress);
}

bool readIndex(const std::string& filepath,
               ChunkIndex& index,
               const std::function<void(float)>& progress) {
    index.clear();
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    const std::uint64_t fileSize = static_cast<std::uint64_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    const std::vector<std::uint8_t> headerBytes = readBytes(file, 0, 24);
    Header header{};
    if (!decodeHeader(headerBytes, header) ||
        header.magic != kMagic ||
        header.version != kVersion ||
        header.tocOffset > fileSize) {
        return false;
    }

    emitProgress(progress, 0.1f);

    const std::uint64_t tocByteCount = static_cast<std::uint64_t>(header.tocCount) * 40U;
    if (header.tocOffset + tocByteCount > fileSize) {
        return false;
    }

    const std::vector<std::uint8_t> tocBytes = readBytes(file, header.tocOffset, tocByteCount);
    if (tocBytes.size() != tocByteCount) {
        return false;
    }

    std::size_t tocOffset = 0;
    for (std::uint32_t entryIndex = 0; entryIndex < header.tocCount; ++entryIndex) {
        TocEntry entry{};
        if (!decodeTocEntry(tocBytes, tocOffset, entry) ||
            entry.offset + entry.storedSize > fileSize ||
            entry.unpackedSize > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return false;
        }
        ChunkLocator locator{};
        locator.tag = entry.tag;
        locator.compression = static_cast<Compression>(entry.compression);
        locator.offset = entry.offset;
        locator.storedSize = entry.storedSize;
        locator.unpackedSize = entry.unpackedSize;
        locator.crc32 = entry.crc32;
        index.emplace(locator.tag, locator);
    }

    emitProgress(progress, 1.0f);
    return true;
}

bool readChunk(const std::string& filepath,
               const ChunkLocator& locator,
               std::vector<std::uint8_t>& payload) {
    payload.clear();

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    const std::uint64_t fileSize = static_cast<std::uint64_t>(file.tellg());
    if (locator.offset + locator.storedSize > fileSize ||
        locator.unpackedSize > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }

    const std::vector<std::uint8_t> storedPayload = readBytes(file, locator.offset, locator.storedSize);
    if (storedPayload.size() != locator.storedSize) {
        return false;
    }

    if (!decompressPayload(
            locator.compression,
            storedPayload,
            static_cast<std::size_t>(locator.unpackedSize),
            payload) ||
        crc32For(payload) != locator.crc32) {
        payload.clear();
        return false;
    }

    return true;
}

bool readFile(const std::string& filepath,
              ChunkMap& chunks,
              const std::function<void(float)>& progress) {
    chunks.clear();

    ChunkIndex index;
    if (!readIndex(
            filepath,
            index,
            [&](const float value) {
                emitProgress(progress, value * 0.1f);
            })) {
        return false;
    }

    std::vector<ChunkLocator> orderedLocators;
    orderedLocators.reserve(index.size());
    for (const auto& [tag, locator] : index) {
        (void)tag;
        orderedLocators.push_back(locator);
    }
    std::sort(
        orderedLocators.begin(),
        orderedLocators.end(),
        [](const ChunkLocator& left, const ChunkLocator& right) {
            return left.offset < right.offset;
        });

    for (std::size_t chunkIndex = 0; chunkIndex < orderedLocators.size(); ++chunkIndex) {
        std::vector<std::uint8_t> payload;
        if (!readChunk(filepath, orderedLocators[chunkIndex], payload)) {
            return false;
        }

        chunks.emplace(orderedLocators[chunkIndex].tag, std::move(payload));
        emitProgress(progress, 0.1f + 0.9f * (static_cast<float>(chunkIndex + 1) / static_cast<float>(std::max<std::size_t>(1, orderedLocators.size()))));
    }

    return true;
}

}

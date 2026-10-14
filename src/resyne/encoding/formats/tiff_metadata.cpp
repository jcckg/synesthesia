#include "resyne/encoding/formats/tiff_metadata.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <span>
#include <vector>

namespace SequenceExporterInternal::TiffMetadata {

namespace {

constexpr std::uint16_t kFirstAudioTag = 65000;
constexpr std::uint16_t kLongType = 4;
constexpr std::uint64_t kMaximumFields = 4096;

using Entry = std::array<std::uint8_t, 20>;

struct Directory {
    bool bigEndian = false;
    bool bigTiff = false;
    std::uint64_t fileSize = 0;
    std::uint64_t nextOffset = 0;
    std::vector<Entry> entries;

    std::size_t countBytes() const { return bigTiff ? 8u : 2u; }
    std::size_t entryBytes() const { return bigTiff ? 20u : 12u; }
    std::size_t offsetBytes() const { return bigTiff ? 8u : 4u; }
    std::size_t valueOffset() const { return bigTiff ? 12u : 8u; }
};

std::uint64_t readUnsigned(std::span<const std::uint8_t> bytes, bool bigEndian) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const std::size_t index = bigEndian ? i : bytes.size() - 1u - i;
        value = (value << 8u) | bytes[index];
    }
    return value;
}

void writeUnsigned(std::span<std::uint8_t> bytes, std::uint64_t value, bool bigEndian) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const std::size_t index = bigEndian ? bytes.size() - 1u - i : i;
        bytes[index] = static_cast<std::uint8_t>(value & 0xffu);
        value >>= 8u;
    }
}

bool readBytes(std::istream& input, std::uint64_t offset, std::span<std::uint8_t> bytes,
               std::uint64_t fileSize) {
    if (offset > fileSize || bytes.size() > fileSize - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return false;
    }
    input.seekg(static_cast<std::streamoff>(offset));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(input);
}

bool readDirectory(std::istream& input, Directory& directory) {
    input.seekg(0, std::ios::end);
    const auto fileSize = input.tellg();
    if (fileSize < 16) {
        return false;
    }
    directory.fileSize = static_cast<std::uint64_t>(fileSize);

    std::array<std::uint8_t, 16> header{};
    if (!readBytes(input, 0, header, directory.fileSize) ||
        !((header[0] == 'I' && header[1] == 'I') || (header[0] == 'M' && header[1] == 'M'))) {
        return false;
    }
    directory.bigEndian = header[0] == 'M';
    const auto version = readUnsigned(std::span(header).subspan(2, 2), directory.bigEndian);
    if (version != 42 && version != 43) {
        return false;
    }
    directory.bigTiff = version == 43;
    if (directory.bigTiff &&
        (readUnsigned(std::span(header).subspan(4, 2), directory.bigEndian) != 8 ||
         readUnsigned(std::span(header).subspan(6, 2), directory.bigEndian) != 0)) {
        return false;
    }

    const auto directoryOffset = readUnsigned(
        std::span(header).subspan(directory.bigTiff ? 8u : 4u, directory.offsetBytes()),
        directory.bigEndian);
    std::array<std::uint8_t, 8> count{};
    const auto countSpan = std::span(count).first(directory.countBytes());
    if (directoryOffset < (directory.bigTiff ? 16u : 8u) ||
        !readBytes(input, directoryOffset, countSpan, directory.fileSize)) {
        return false;
    }
    const auto fieldCount = readUnsigned(countSpan, directory.bigEndian);
    if (fieldCount > kMaximumFields) {
        return false;
    }
    const auto tableOffset = directoryOffset + directory.countBytes();
    const auto tableSize = fieldCount * directory.entryBytes() + directory.offsetBytes();
    if (tableOffset > directory.fileSize || tableSize > directory.fileSize - tableOffset) {
        return false;
    }

    directory.entries.resize(static_cast<std::size_t>(fieldCount));
    for (std::size_t i = 0; i < directory.entries.size(); ++i) {
        if (!readBytes(input, tableOffset + i * directory.entryBytes(),
                       std::span(directory.entries[i]).first(directory.entryBytes()), directory.fileSize)) {
            return false;
        }
    }
    const auto offsetSpan = std::span(count).first(directory.offsetBytes());
    if (!readBytes(input, tableOffset + fieldCount * directory.entryBytes(), offsetSpan, directory.fileSize)) {
        return false;
    }
    directory.nextOffset = readUnsigned(offsetSpan, directory.bigEndian);
    return true;
}

std::uint16_t tagFor(const Entry& entry, bool bigEndian) {
    return static_cast<std::uint16_t>(readUnsigned(std::span(entry).first(2), bigEndian));
}

}

AudioFields read(const std::string& filepath) {
    std::ifstream input(filepath, std::ios::binary);
    Directory directory;
    AudioFields metadata;
    if (!readDirectory(input, directory)) {
        return metadata;
    }
    const std::array<std::uint32_t*, 4> fields{
        &metadata.sampleRate, &metadata.fftSize, &metadata.hopSize, &metadata.channels
    };
    for (const auto& entry : directory.entries) {
        const auto bytes = std::span(entry);
        const auto tag = tagFor(entry, directory.bigEndian);
        if (tag < kFirstAudioTag || tag >= kFirstAudioTag + fields.size() ||
            readUnsigned(bytes.subspan(2, 2), directory.bigEndian) != kLongType ||
            readUnsigned(bytes.subspan(4, directory.bigTiff ? 8u : 4u), directory.bigEndian) != 1) {
            continue;
        }
        *fields[tag - kFirstAudioTag] = static_cast<std::uint32_t>(
            readUnsigned(bytes.subspan(directory.valueOffset(), 4), directory.bigEndian));
    }
    return metadata;
}

bool write(const std::string& filepath, const AudioFields& metadata) {
    std::fstream output(filepath, std::ios::binary | std::ios::in | std::ios::out);
    Directory directory;
    if (!readDirectory(output, directory)) {
        return false;
    }
    std::erase_if(directory.entries, [&](const Entry& entry) {
        const auto tag = tagFor(entry, directory.bigEndian);
        return tag >= kFirstAudioTag && tag < kFirstAudioTag + 4u;
    });
    const std::array<std::uint32_t, 4> fields{
        metadata.sampleRate, metadata.fftSize, metadata.hopSize, metadata.channels
    };
    for (std::size_t i = 0; i < fields.size(); ++i) {
        Entry entry{};
        const auto bytes = std::span(entry);
        writeUnsigned(bytes.first(2), kFirstAudioTag + i, directory.bigEndian);
        writeUnsigned(bytes.subspan(2, 2), kLongType, directory.bigEndian);
        writeUnsigned(bytes.subspan(4, directory.bigTiff ? 8u : 4u), 1, directory.bigEndian);
        writeUnsigned(bytes.subspan(directory.valueOffset(), 4), fields[i], directory.bigEndian);
        directory.entries.push_back(entry);
    }
    std::sort(directory.entries.begin(), directory.entries.end(), [&](const Entry& left, const Entry& right) {
        return tagFor(left, directory.bigEndian) < tagFor(right, directory.bigEndian);
    });

    const std::uint64_t alignment = directory.bigTiff ? 8u : 2u;
    const auto directoryOffset = (directory.fileSize + alignment - 1u) & ~(alignment - 1u);
    const auto directorySize = directory.countBytes() + directory.entries.size() * directory.entryBytes() +
        directory.offsetBytes();
    const auto maximumOffset = directory.bigTiff
        ? static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())
        : static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    if (directory.entries.size() > kMaximumFields || directoryOffset > maximumOffset ||
        directorySize > maximumOffset - directoryOffset) {
        return false;
    }

    std::vector<std::uint8_t> bytes(directorySize);
    writeUnsigned(std::span(bytes).first(directory.countBytes()), directory.entries.size(), directory.bigEndian);
    std::size_t position = directory.countBytes();
    for (const auto& entry : directory.entries) {
        std::copy_n(entry.begin(), directory.entryBytes(), bytes.begin() + static_cast<std::ptrdiff_t>(position));
        position += directory.entryBytes();
    }
    writeUnsigned(std::span(bytes).subspan(position, directory.offsetBytes()), directory.nextOffset, directory.bigEndian);
    output.seekp(static_cast<std::streamoff>(directoryOffset));
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
        return false;
    }

    std::array<std::uint8_t, 8> offset{};
    writeUnsigned(std::span(offset).first(directory.offsetBytes()), directoryOffset, directory.bigEndian);
    output.seekp(directory.bigTiff ? 8 : 4);
    output.write(reinterpret_cast<const char*>(offset.data()), static_cast<std::streamsize>(directory.offsetBytes()));
    output.flush();
    return static_cast<bool>(output);
}

}

#pragma once

#include "utilities/cancellation.h"
#include <algorithm>
#include <istream>
#include <ostream>

namespace Utilities {
inline bool readBytes(std::istream& stream, void* data, std::size_t size) {
    auto* bytes = static_cast<char*>(data);
    for (std::size_t offset = 0; offset < size;) {
        checkCancellation();
        const auto count = std::min<std::size_t>(size - offset, 1024 * 1024);
        if (!stream.read(bytes + offset, static_cast<std::streamsize>(count))) return false;
        offset += count;
    }
    return true;
}
inline bool writeBytes(std::ostream& stream, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    for (std::size_t offset = 0; offset < size;) {
        checkCancellation();
        const auto count = std::min<std::size_t>(size - offset, 1024 * 1024);
        if (!stream.write(bytes + offset, static_cast<std::streamsize>(count))) return false;
        offset += count;
    }
    return true;
}
}

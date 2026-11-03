#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Utilities {
class DatagramSocket {
public:
    DatagramSocket(std::uint32_t address, std::uint16_t port);
    ~DatagramSocket();
    bool send(const void* data, std::size_t size);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

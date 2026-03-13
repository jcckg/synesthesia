#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Synesthesia::OSC {

inline constexpr const char* kLoopbackHost = "127.0.0.1";

struct OSCConfig {
    std::string destinationHost = kLoopbackHost;
    uint16_t transmitPort = 7000;
    uint16_t receivePort = 7001;
    std::size_t outputBufferSize = 4096;
};

struct OSCDestinationValidationResult {
    bool valid = false;
    uint32_t address = 0;
    std::string canonicalHost;
    std::string errorMessage;
};

OSCDestinationValidationResult validateOSCDestination(const std::string& destinationHost);

}

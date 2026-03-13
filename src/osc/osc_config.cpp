#include "osc_config.h"

#include <array>
#include <cctype>
#include <string_view>

namespace Synesthesia::OSC {

namespace {

std::string trimWhitespace(const std::string_view value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

bool parseIPv4Literal(const std::string_view input, std::array<uint8_t, 4>& octets) {
    size_t octetIndex = 0;
    size_t current = 0;

    while (current < input.size() && octetIndex < octets.size()) {
        if (!std::isdigit(static_cast<unsigned char>(input[current]))) {
            return false;
        }

        unsigned int value = 0;
        size_t digitCount = 0;
        while (current < input.size() && std::isdigit(static_cast<unsigned char>(input[current])) != 0) {
            value = value * 10u + static_cast<unsigned int>(input[current] - '0');
            if (value > 255u) {
                return false;
            }
            ++current;
            ++digitCount;
        }

        if (digitCount == 0) {
            return false;
        }

        octets[octetIndex++] = static_cast<uint8_t>(value);

        if (octetIndex == octets.size()) {
            break;
        }

        if (current >= input.size() || input[current] != '.') {
            return false;
        }
        ++current;
        if (current >= input.size()) {
            return false;
        }
    }

    return octetIndex == octets.size() && current == input.size();
}

bool isPrivateOrLoopbackIPv4(const std::array<uint8_t, 4>& octets) {
    if (octets[0] == 127) {
        return true;
    }
    if (octets[0] == 10) {
        return true;
    }
    if (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) {
        return true;
    }
    if (octets[0] == 192 && octets[1] == 168) {
        return true;
    }
    return false;
}

}  // namespace

OSCDestinationValidationResult validateOSCDestination(const std::string& destinationHost) {
    OSCDestinationValidationResult result;

    const std::string trimmedHost = trimWhitespace(destinationHost);
    if (trimmedHost.empty()) {
        result.errorMessage = "Destination is required";
        return result;
    }

    std::array<uint8_t, 4> octets{};
    if (!parseIPv4Literal(trimmedHost, octets)) {
        result.errorMessage = "Destination must be a numeric IPv4 address";
        return result;
    }

    if (!isPrivateOrLoopbackIPv4(octets)) {
        result.errorMessage = "Destination must be loopback or RFC1918 private IPv4";
        return result;
    }

    result.valid = true;
    result.address =
        (static_cast<uint32_t>(octets[0]) << 24) |
        (static_cast<uint32_t>(octets[1]) << 16) |
        (static_cast<uint32_t>(octets[2]) << 8) |
        static_cast<uint32_t>(octets[3]);
    result.canonicalHost =
        std::to_string(octets[0]) + "." +
        std::to_string(octets[1]) + "." +
        std::to_string(octets[2]) + "." +
        std::to_string(octets[3]);

    return result;
}

}

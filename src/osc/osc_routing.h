#pragma once

#include "osc_messages.h"

#include <map>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Synesthesia::OSC {

using OSCArgument = std::variant<int32_t, int64_t, float, bool, std::string>;

struct OSCOutput {
    std::string address;
    OSCArgument value = 0.0f;
    std::vector<float> samples;
    bool vector = false;
    bool available = true;
    std::size_t sampleCount = 0;
    std::size_t length() const { return samples.empty() ? sampleCount : samples.size(); }
};

struct OSCRoute {
    std::string address;
    std::string name;
    std::string group;
    std::string type;
    bool builtIn = true;
    bool supportsSmoothing = false;
};

struct OSCRouteSettings {
    bool enabled = false;
    bool smoothing = false;
};

using OSCRouting = std::map<std::string, OSCRouteSettings>;

struct OSCPacket {
    std::vector<char> bytes;
};

struct OSCOutputSize {
    std::size_t bytes = 0;
    std::size_t packets = 0;
    std::size_t largestPacket = 0;
};

inline constexpr std::size_t kOptionalPacketLimit = 1200;

const std::vector<OSCRoute>& outputCatalogue();
OSCRouting defaultRouting();
std::vector<OSCOutput> buildOutputs(const OSCFrameData& frame, const OSCRouting& routing, bool includeDisabled = true, bool includeSamples = true);
std::vector<OSCPacket> encodePackets(const OSCFrameData& frame, std::span<const OSCOutput> outputs, const OSCRouting& routing, std::size_t scalarBufferSize = 8192);
OSCOutputSize outputSize(const OSCOutput& output);

}

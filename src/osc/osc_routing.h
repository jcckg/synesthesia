#pragma once

#include "osc_messages.h"

#include <map>
#include <array>
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

inline constexpr std::size_t kPacketLimit = 1400;
inline constexpr std::size_t kSpectrumChunkSize = 240;
inline constexpr uint64_t kFrameIndexModulus = uint64_t{1} << 24;
inline constexpr const char* kBandFrequenciesAddress = "/synesthesia/frame/contribution_bands/frequencies_hz";
inline constexpr const char* kFrameContextAddress = "/synesthesia/frame/meta/context";
inline constexpr std::array<const char*, 15> kVectorAddresses{
    "/synesthesia/frame/spectrum/frequencies_hz",
    "/synesthesia/frame/spectrum/magnitudes",
    "/synesthesia/frame/spectrum/phases_radians",
    kBandFrequenciesAddress,
    "/synesthesia/frame/contribution_bands/magnitudes",
    "/synesthesia/frame/contribution_bands/cie_x",
    "/synesthesia/frame/contribution_bands/cie_y",
    "/synesthesia/frame/contribution_bands/cie_z",
    "/synesthesia/frame/contribution_bands/response_seconds",
    "/synesthesia/frame/contribution_bands/spectral_flux",
    "/synesthesia/frame/contribution_bands/onset_detected",
    "/synesthesia/frame/contribution_bands/levels_normalised",
    "/synesthesia/frame/contribution_bands/linear_r",
    "/synesthesia/frame/contribution_bands/linear_g",
    "/synesthesia/frame/contribution_bands/linear_b"
};

constexpr int32_t frameIndex(const OSCFrameData& frame) {
    return static_cast<int32_t>(frame.frameId % kFrameIndexModulus);
}

struct OSCEncodingOptions {
    std::size_t packetLimit = kPacketLimit;
    bool includeBandFrequencies = true;
};

const std::vector<OSCRoute>& outputCatalogue();
OSCRouting defaultRouting();
std::vector<OSCOutput> buildOutputs(const OSCFrameData& frame, const OSCRouting& routing, bool includeDisabled = true, bool includeSamples = true, bool smoothBandMagnitudes = false, bool includeBandFrequencies = true);
std::vector<OSCPacket> encodePackets(const OSCFrameData& frame, std::span<const OSCOutput> outputs, const OSCRouting& routing, const OSCEncodingOptions& options = {});
OSCOutputSize outputSize(const OSCFrameData& frame, std::span<const OSCOutput> outputs, const OSCRouting& routing, const OSCEncodingOptions& options = {});
OSCOutputSize outputSize(const OSCOutput& output, const OSCFrameData& frame = {});

}

#include "osc_routing.h"

#include "osc_addresses.h"
#include "osc_analysis_fields.h"
#include "ui/smoothing/smoothing.h"
#include "osc/OscOutboundPacketStream.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <type_traits>

namespace Synesthesia::OSC {

namespace {

void appendFloatValue(std::vector<OSCOutput>& outputs, const char* address, float value) {
    outputs.push_back({address, value, {}, false, true});
}

void appendInt32Value(std::vector<OSCOutput>& outputs, const char* address, int32_t value) {
    outputs.push_back({address, value, {}, false, true});
}

void appendInt64Value(std::vector<OSCOutput>& outputs, const char* address, int64_t value) {
    outputs.push_back({address, value, {}, false, true});
}

void appendBoolValue(std::vector<OSCOutput>& outputs, const char* address, bool value) {
    outputs.push_back({address, value, {}, false, true});
}

void appendStringValue(std::vector<OSCOutput>& outputs, const char* address, const char* value) {
    outputs.push_back({address, std::string(value), {}, false, true});
}

template <typename Data, std::size_t Size>
void appendFields(std::vector<OSCOutput>& outputs, const Data& data,
                  const std::array<OSCFloatField<Data>, Size>& fields, bool source = false) {
    for (const auto& field : fields) appendFloatValue(outputs, source ? field.sourceAddress : field.legacyAddress, data.*field.value);
}

std::vector<OSCOutput> builtInOutputs(const OSCFrameData& frame) {
    std::vector<OSCOutput> packet;
    appendStringValue(packet, kFrameStatusAddress, statusName(frame.status));
    appendInt32Value(packet, kFrameMetaSampleRateAddress, frame.meta.sampleRate);
    appendInt32Value(packet, kFrameMetaFftSizeAddress, frame.meta.fftSize);
    appendInt64Value(packet, kFrameMetaTimestampAddress, frame.meta.frameTimestamp);

    appendBoolValue(packet, kFrameSmoothingEnabledAddress, frame.smoothing.enabled);
    appendStringValue(packet, kFrameSmoothingModeAddress, smoothingMode(frame.smoothing));
    appendFloatValue(packet, kFrameSmoothingAmountAddress, frame.smoothing.amount);
    appendFloatValue(packet, kFrameSmoothingManualResponseAddress, manualResponseTime(frame.smoothing.amount));
    appendFloatValue(packet, kFrameSmoothingResponseAddress, frame.smoothing.responseSeconds);
    appendFloatValue(packet, kFrameSmoothingMinimumResponseAddress, frame.smoothing.minimumResponseSeconds);
    appendFloatValue(packet, kFrameSmoothingMaximumResponseAddress, frame.smoothing.maximumResponseSeconds);

    appendBoolValue(packet, kFrameSourceAnalysisAvailableAddress, frame.sourceAnalysis.available);
    appendFields(packet, frame.signal, signalFields, true);
    appendFields(packet, frame.spectral, spectralFields, true);
    appendFields(packet, frame.loudness, loudnessFields, true);
    appendFields(packet, frame.phase, phaseFields, true);
    for (const auto& field : colourAnalysisFields) appendFloatValue(packet, field.sourceAddress, frame.sourceAnalysis.*field.value);

    appendFloatValue(packet, kFrameColourDisplayRAddress, frame.colour.displayR);
    appendFloatValue(packet, kFrameColourDisplayGAddress, frame.colour.displayG);
    appendFloatValue(packet, kFrameColourDisplayBAddress, frame.colour.displayB);
    appendFloatValue(packet, kFrameColourCIEXAddress, frame.colour.cieX);
    appendFloatValue(packet, kFrameColourCIEYAddress, frame.colour.cieY);
    appendFloatValue(packet, kFrameColourCIEZAddress, frame.colour.cieZ);
    appendFloatValue(packet, kFrameMeasurementCIE2015XAddress, frame.colour.measurementXYZ.X);
    appendFloatValue(packet, kFrameMeasurementCIE2015YAddress, frame.colour.measurementXYZ.Y);
    appendFloatValue(packet, kFrameMeasurementCIE2015ZAddress, frame.colour.measurementXYZ.Z);
    appendBoolValue(packet, kFrameMeasurementAvailableAddress, frame.colour.measurementAvailable);
    appendFloatValue(packet, kFrameColourOklabLAddress, frame.colour.oklabL);
    appendFloatValue(packet, kFrameColourOklabAAddress, frame.colour.oklabA);
    appendFloatValue(packet, kFrameColourOklabBAddress, frame.colour.oklabB);

    appendFields(packet, frame.signal, signalFields);
    appendFields(packet, frame.spectral, spectralFields);
    appendFields(packet, frame.loudness, loudnessFields);
    appendFields(packet, frame.phase, phaseFields);
    appendFloatValue(packet, kFrameAnalysisSpectralFluxAddress, frame.spectral.spectralFlux);
    appendBoolValue(packet, kFrameAnalysisOnsetDetectedAddress, frame.transient.onsetDetected);
    return packet;
}

std::size_t padded(std::size_t length) {
    return (length + 3) & ~std::size_t(3);
}

std::string outputName(const std::string& address) {
    auto name = address.substr(address.find_last_of('/') + 1);
    std::replace(name.begin(), name.end(), '_', ' ');
    if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

std::string outputGroup(const std::string& address) {
    if (address.find("/meta/") != std::string::npos) return "Frame metadata";
    if (address.find("/smoothing/") != std::string::npos) return "Smoothing";
    if (address.find("/colour/") != std::string::npos) return "Colour";
    if (address.find("/measurement/") != std::string::npos) return "Measurement";
    if (address.find("/signal/") != std::string::npos) return "Signal";
    if (address.find("/analysis/") != std::string::npos) return "Analysis";
    return "Status";
}

const OSCColourAnalysisField* colourAnalysisField(const std::string& address) {
    const auto field = std::find_if(colourAnalysisFields.begin(), colourAnalysisFields.end(),
        [&](const auto& candidate) { return address == candidate.sourceAddress; });
    return field == colourAnalysisFields.end() ? nullptr : &*field;
}

std::string scalarType(const OSCArgument& value) {
    return std::visit([](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, int32_t>) return "int32";
        if constexpr (std::is_same_v<T, int64_t>) return "int64";
        if constexpr (std::is_same_v<T, float>) return "float32";
        if constexpr (std::is_same_v<T, bool>) return "bool";
        return "string";
    }, value);
}

void appendArgument(osc::OutboundPacketStream& packet, const OSCArgument& value) {
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::string>) packet << item.c_str();
        else if constexpr (std::is_same_v<T, int64_t>) packet << static_cast<osc::int64>(item);
        else if constexpr (std::is_same_v<T, int32_t>) packet << static_cast<osc::int32>(item);
        else packet << item;
    }, value);
}

void appendScalar(osc::OutboundPacketStream& packet, const OSCOutput& output) {
    packet << osc::BeginMessage(output.address.c_str());
    appendArgument(packet, output.value);
    packet << osc::EndMessage;
}

std::size_t chunkLength(const std::string& address, std::size_t remaining) {
    auto count = std::min<std::size_t>(remaining, kOptionalPacketLimit / 5);
    while (count > 0 && padded(address.size() + 1) + padded(count + 7) + 32 + count * 4 > kOptionalPacketLimit) --count;
    return count;
}

OSCPacket encodeChunk(const OSCFrameData& frame, const OSCOutput& output, std::size_t offset, std::size_t count) {
    OSCPacket result;
    result.bytes.resize(kOptionalPacketLimit);
    osc::OutboundPacketStream packet(result.bytes.data(), result.bytes.size());
    packet << osc::BeginMessage(output.address.c_str())
           << static_cast<osc::int64>(frame.frameId)
           << static_cast<osc::int64>(frame.sourceId)
           << static_cast<osc::int64>(frame.meta.frameTimestamp)
           << static_cast<osc::int32>(offset)
           << static_cast<osc::int32>(output.samples.size());
    for (std::size_t i = 0; i < count; ++i) packet << output.samples[offset + i];
    packet << osc::EndMessage;
    result.bytes.resize(packet.Size());
    return result;
}

}

const std::vector<OSCRoute>& outputCatalogue() {
    static const auto catalogue = [] {
        std::vector<OSCRoute> result;
        for (const auto& output : builtInOutputs({}))
            result.push_back({output.address, outputName(output.address), outputGroup(output.address), scalarType(output.value),
                true, output.address.find("/colour/") != std::string::npos ||
                    output.address == kFrameSourceAnalysisAvailableAddress || colourAnalysisField(output.address) != nullptr});
        const std::string spectrum = "/synesthesia/frame/spectrum/";
        const std::string bands = "/synesthesia/frame/contribution_bands/";
        result.push_back({spectrum + "frequencies_hz", "Frequencies", "Spectrum", "float32[]", false});
        result.push_back({spectrum + "magnitudes", "Magnitudes", "Spectrum", "float32[]", false});
        result.push_back({spectrum + "phases_radians", "Phases", "Spectrum", "float32[]", false});
        result.push_back({bands + "frequencies_hz", "Frequencies", "Contribution bands", "float32[]", false});
        result.push_back({bands + "magnitudes", "Magnitudes", "Contribution bands", "float32[]", false});
        result.push_back({bands + "cie_x", "CIE X", "Contribution bands", "float32[]", false, true});
        result.push_back({bands + "cie_y", "CIE Y", "Contribution bands", "float32[]", false, true});
        result.push_back({bands + "cie_z", "CIE Z", "Contribution bands", "float32[]", false, true});
        result.push_back({bands + "response_seconds", "Response time", "Contribution bands", "float32[]", false});
        return result;
    }();
    return catalogue;
}

OSCRouting defaultRouting() {
    OSCRouting result;
    for (const auto& route : outputCatalogue())
        result.emplace(route.address, OSCRouteSettings{route.builtIn, route.builtIn && route.address.find("/colour/") != std::string::npos});
    return result;
}

std::vector<OSCOutput> buildOutputs(const OSCFrameData& frame, const OSCRouting& routing, bool includeDisabled, bool includeSamples) {
    auto result = builtInOutputs(frame);
    for (auto& output : result) {
        const auto setting = routing.find(output.address);
        const auto* field = colourAnalysisField(output.address);
        const bool smoothing = setting != routing.end() && setting->second.smoothing;
        const auto& analysis = smoothing ? frame.presentedAnalysis : frame.sourceAnalysis;
        if (output.address == kFrameSourceAnalysisAvailableAddress) output.value = analysis.available;
        else if (field != nullptr) {
            output.value = analysis.available ? analysis.*field->value : std::numeric_limits<float>::quiet_NaN();
            output.available = analysis.available;
        }
        if (output.address.find("/colour/") != std::string::npos && setting != routing.end() && !setting->second.smoothing) {
            const auto& colour = frame.sourceColour;
            const auto& address = output.address;
            if (address.ends_with("/display_r")) output.value = colour.displayR;
            else if (address.ends_with("/display_g")) output.value = colour.displayG;
            else if (address.ends_with("/display_b")) output.value = colour.displayB;
            else if (address.ends_with("/cie_x")) output.value = colour.cieX;
            else if (address.ends_with("/cie_y")) output.value = colour.cieY;
            else if (address.ends_with("/cie_z")) output.value = colour.cieZ;
            else if (address.ends_with("/oklab_l")) output.value = colour.oklabL;
            else if (address.ends_with("/oklab_a")) output.value = colour.oklabA;
            else if (address.ends_with("/oklab_b")) output.value = colour.oklabB;
            output.available = frame.sourceAnalysis.available;
            if (!output.available) output.value = std::numeric_limits<float>::quiet_NaN();
        }
        if (output.address.find("/analysis/source/") != std::string::npos && field == nullptr)
            output.available = frame.sourceAnalysis.available;
        if (output.address.ends_with("/available")) output.available = true;
    }
    if (!includeDisabled && std::none_of(outputCatalogue().begin(), outputCatalogue().end(), [&](const auto& route) {
        const auto setting = routing.find(route.address);
        return !route.builtIn && setting != routing.end() && setting->second.enabled;
    })) return result;
    const auto enabled = [&](const std::string& address) {
        const auto setting = routing.find(address);
        return includeDisabled || (setting != routing.end() && setting->second.enabled);
    };
    const auto addVector = [&](const std::string& address, auto&& makeValues, bool available, std::size_t length = ColourPresentation::contributionBandCount) {
        if (!enabled(address)) return;
        OSCOutput output;
        output.address = address;
        output.sampleCount = length;
        if (includeSamples) output.samples = makeValues();
        output.vector = true;
        output.available = available;
        result.push_back(std::move(output));
    };
    const std::string spectrum = "/synesthesia/frame/spectrum/";
    const bool frequenciesAvailable = frame.frequencies.empty() ? frame.meta.fftSize > 0 : frame.frequencies.size() == frame.magnitudes.size();
    const bool spectrumAvailable = frame.sourceAvailable && !frame.magnitudes.empty() && frequenciesAvailable;
    addVector(spectrum + "frequencies_hz", [&] {
        if (!frame.frequencies.empty()) return frame.frequencies;
        std::vector<float> frequencies;
        if (frame.meta.fftSize > 0) {
            frequencies.resize(frame.magnitudes.size());
            for (std::size_t i = 0; i < frequencies.size(); ++i)
                frequencies[i] = static_cast<float>(i) * static_cast<float>(frame.meta.sampleRate) / static_cast<float>(frame.meta.fftSize);
        }
        return frequencies;
    }, spectrumAvailable, frame.frequencies.empty() ? (frame.meta.fftSize > 0 ? frame.magnitudes.size() : 0) : frame.frequencies.size());
    addVector(spectrum + "magnitudes", [&] { return frame.magnitudes; }, spectrumAvailable, frame.magnitudes.size());
    addVector(spectrum + "phases_radians", [&] { return frame.phases; }, spectrumAvailable && frame.phases.size() == frame.magnitudes.size(), frame.phases.size());
    const std::string bands = "/synesthesia/frame/contribution_bands/";
    addVector(bands + "frequencies_hz", [] {
        static const auto frequencies = [] {
            std::vector<float> values(ColourPresentation::contributionBandCount);
            for (std::size_t i = 0; i < values.size(); ++i) values[i] = ColourPresentation::contributionBandFrequency(i);
            return values;
        }();
        return frequencies;
    }, frame.sourceContributions != nullptr);
    for (int component = -1; component < 4; ++component) {
        const auto address = bands + (component == -1 ? "magnitudes" : component == 3 ? "response_seconds" : component == 0 ? "cie_x" : component == 1 ? "cie_y" : "cie_z");
        if (!enabled(address)) continue;
        const auto setting = routing.find(address);
        const bool smoothing = component >= 0 && component < 3 && setting != routing.end() && setting->second.smoothing;
        const auto& contributions = smoothing || component == 3 ? frame.presentedContributions : frame.sourceContributions;
        addVector(address, [&] {
            std::vector<float> values(ColourPresentation::contributionBandCount, 0.0f);
            if (contributions) {
                for (const auto& band : *contributions) {
                    if (band.band >= values.size()) continue;
                    if (component == -1) values[band.band] += band.magnitude;
                    else if (component == 3) values[band.band] = band.responseSeconds;
                    else values[band.band] += band.xyz[static_cast<std::size_t>(component)];
                }
            }
            return values;
        }, contributions != nullptr);
    }
    return result;
}

std::vector<OSCPacket> encodePackets(const OSCFrameData& frame, std::span<const OSCOutput> outputs, const OSCRouting& routing, std::size_t scalarBufferSize) {
    std::vector<OSCPacket> result;
    OSCPacket builtIn;
    builtIn.bytes.resize(scalarBufferSize);
    osc::OutboundPacketStream packet(builtIn.bytes.data(), builtIn.bytes.size());
    packet << osc::BeginBundleImmediate;
    std::size_t scalarCount = 0;
    for (const auto& output : outputs) {
        const auto setting = routing.find(output.address);
        if (setting == routing.end() || !setting->second.enabled || output.vector) continue;
        appendScalar(packet, output);
        ++scalarCount;
    }
    packet << osc::EndBundle;
    if (scalarCount > 0) {
        builtIn.bytes.resize(packet.Size());
        result.push_back(std::move(builtIn));
    }
    for (const auto& output : outputs) {
        const auto setting = routing.find(output.address);
        if (setting == routing.end() || !setting->second.enabled || !output.vector) continue;
        if (!output.available || output.samples.empty()) {
            auto unavailable = output;
            unavailable.samples.clear();
            result.push_back(encodeChunk(frame, unavailable, 0, 0));
            continue;
        }
        for (std::size_t offset = 0; offset < output.samples.size();) {
            const auto count = chunkLength(output.address, output.samples.size() - offset);
            if (count == 0) throw std::length_error("OSC address exceeds the packet limit");
            result.push_back(encodeChunk(frame, output, offset, count));
            offset += count;
        }
    }
    return result;
}

OSCOutputSize outputSize(const OSCOutput& output) {
    if (!output.vector) {
        std::array<char, 2048> buffer{};
        osc::OutboundPacketStream packet(buffer.data(), buffer.size());
        appendScalar(packet, output);
        return {packet.Size() + 4, 0, 0};
    }
    OSCOutputSize result;
    auto size = output.available ? output.length() : 0;
    std::size_t offset = 0;
    do {
        const auto count = chunkLength(output.address, size - offset);
        const auto bytes = padded(output.address.size() + 1) + padded(count + 7) + 32 + count * 4;
        result.bytes += bytes;
        ++result.packets;
        result.largestPacket = std::max(result.largestPacket, bytes);
        if (count == 0) break;
        offset += count;
    } while (offset < size);
    return result;
}

}

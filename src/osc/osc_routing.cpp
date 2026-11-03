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
#include <optional>
#include <type_traits>
#include <string_view>

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
    appendInt32Value(packet, kFrameMetaFrameIndexAddress, frameIndex(frame));
    return packet;
}

constexpr std::size_t padded(std::size_t length) {
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

constexpr std::size_t kBundleHeaderSize = 16;
constexpr std::size_t kContextMessageSize = padded(std::string_view(kFrameContextAddress).size() + 1) +
    padded(sizeof("ihhhiiiii") + 1) + sizeof(int32_t) * 6 + sizeof(int64_t) * 3;
constexpr std::size_t kPacketHeaderSize = kBundleHeaderSize + 4 + kContextMessageSize;

std::size_t scalarSize(const OSCOutput& output) {
    const auto size = std::visit([](const auto& value) -> std::size_t {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::string>) return padded(value.size() + 1);
        if constexpr (std::is_same_v<T, bool>) return 0;
        return sizeof(T);
    }, output.value);
    return padded(output.address.size() + 1) + 4 + size;
}

std::size_t arraySize(std::string_view address, std::size_t count) {
    return padded(address.size() + 1) + padded(count + 3) + 4 + count * 4;
}

bool selected(const OSCOutput& output, const OSCRouting& routing, const OSCEncodingOptions& options) {
    const auto setting = routing.find(output.address);
    return setting != routing.end() && setting->second.enabled &&
        (options.includeBandFrequencies || output.address != kBandFrequenciesAddress);
}

bool spectrumOutput(const OSCOutput& output) {
    return output.address.starts_with("/synesthesia/frame/spectrum/");
}

std::size_t spectrumLength(const OSCFrameData& frame, std::span<const OSCOutput> outputs) {
    if (!frame.magnitudes.empty()) return frame.magnitudes.size();
    if (frame.meta.fftSize > 0) return static_cast<std::size_t>(frame.meta.fftSize) / 2 + 1;
    std::size_t count = 0;
    for (const auto& output : outputs)
        if (output.vector && spectrumOutput(output)) count = std::max(count, output.length());
    return count;
}

std::size_t spectrumChunks(std::size_t count, std::size_t limit) {
    if (count == 0) return 1;
    if (kPacketHeaderSize + 4 + arraySize(kVectorAddresses[0], count) <= limit) return 1;
    return (count + kSpectrumChunkSize - 1) / kSpectrumChunkSize;
}

struct FrameContext {
    int32_t arrayMask = 0;
    int32_t availableMask = 0;
    int32_t spectrumCount = 0;
    int32_t spectrumChunks = 1;
};

FrameContext frameContext(const OSCFrameData& frame, std::span<const OSCOutput> outputs,
                          const OSCRouting& routing, const OSCEncodingOptions& options) {
    FrameContext context;
    const auto count = spectrumLength(frame, outputs);
    if (count > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
        throw std::length_error("OSC spectrum exceeds the supported length");
    context.spectrumCount = static_cast<int32_t>(count);
    context.spectrumChunks = static_cast<int32_t>(spectrumChunks(count, std::min(options.packetLimit, kPacketLimit)));
    for (const auto& output : outputs) {
        if (!output.vector || !selected(output, routing, options)) continue;
        const auto found = std::find(kVectorAddresses.begin(), kVectorAddresses.end(), output.address);
        if (found == kVectorAddresses.end()) throw std::invalid_argument("Unrecognised OSC array: " + output.address);
        const auto bit = int32_t{1} << static_cast<unsigned>(found - kVectorAddresses.begin());
        context.arrayMask |= bit;
        const auto expected = spectrumOutput(output) ? count : ColourPresentation::contributionBandCount;
        if (output.available && expected > 0 && output.length() == expected) context.availableMask |= bit;
    }
    return context;
}

void appendContext(osc::OutboundPacketStream& packet, const OSCFrameData& frame, const FrameContext& context) {
    packet << osc::BeginMessage(kFrameContextAddress)
           << static_cast<osc::int32>(frameIndex(frame))
           << static_cast<osc::int64>(frame.sessionId)
           << static_cast<osc::int64>(frame.sourceId)
           << static_cast<osc::int64>(frame.meta.frameTimestamp)
           << static_cast<osc::int32>(context.arrayMask)
           << static_cast<osc::int32>(context.availableMask)
           << static_cast<osc::int32>(context.spectrumCount)
           << static_cast<osc::int32>(context.spectrumChunks)
           << static_cast<osc::int32>(ColourPresentation::contributionBandCount)
           << osc::EndMessage;
}

template <typename Begin, typename Message, typename End>
OSCOutputSize visitPackets(const OSCFrameData& frame, std::span<const OSCOutput> outputs,
                          const OSCRouting& routing, const OSCEncodingOptions& options,
                          Begin&& begin, Message&& message, End&& end) {
    const auto limit = std::min(options.packetLimit, kPacketLimit);
    const auto context = frameContext(frame, outputs, routing, options);
    OSCOutputSize result;
    std::size_t bytes = 0;
    const auto finish = [&] {
        if (bytes == 0) return;
        end();
        result.bytes += bytes;
        ++result.packets;
        result.largestPacket = std::max(result.largestPacket, bytes);
        bytes = 0;
    };
    const auto append = [&](const OSCOutput& output, std::string_view address, std::size_t offset, std::size_t count) {
        const auto size = 4 + (output.vector ? arraySize(address, count) : scalarSize(output));
        if (kPacketHeaderSize + size > limit) throw std::length_error("OSC message exceeds the packet limit");
        if (bytes != 0 && bytes + size > limit) finish();
        if (bytes == 0) {
            begin(context, limit);
            bytes = kPacketHeaderSize;
        }
        bool available = true;
        if (output.vector) {
            const auto found = std::find(kVectorAddresses.begin(), kVectorAddresses.end(), output.address);
            available = found != kVectorAddresses.end() &&
                (context.availableMask & (int32_t{1} << static_cast<unsigned>(found - kVectorAddresses.begin()))) != 0;
        }
        message(output, address, offset, count, available);
        bytes += size;
    };
    for (const auto& output : outputs) {
        if (!selected(output, routing, options)) continue;
        if (!output.vector) {
            append(output, output.address, 0, 0);
            continue;
        }
        const auto count = spectrumOutput(output) ? static_cast<std::size_t>(context.spectrumCount) : ColourPresentation::contributionBandCount;
        if (!spectrumOutput(output) || context.spectrumChunks == 1) {
            append(output, output.address, 0, count);
            continue;
        }
        for (std::size_t offset = 0; offset < count; offset += kSpectrumChunkSize) {
            const auto address = output.address + "/" + std::to_string(offset / kSpectrumChunkSize);
            append(output, address, offset, std::min(kSpectrumChunkSize, count - offset));
        }
    }
    finish();
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
        result.push_back({bands + "spectral_flux", "Spectral flux", "Contribution bands", "float32[]", false});
        result.push_back({bands + "onset_detected", "Onset detected", "Contribution bands", "float32[]", false});
        result.push_back({bands + "levels_normalised", "Normalised levels", "Contribution bands", "float32[]", false});
        for (const auto* component : {"r", "g", "b"})
            result.push_back({bands + "linear_" + component, std::string("Linear ") + component, "Contribution bands", "float32[]", false, true});
        result.push_back({bands + "frame_number", "Frame number", "Contribution bands", "int64", false});
        for (const auto* component : {"r", "g", "b"})
            result.push_back({std::string("/synesthesia/frame/colour/linear_") + component, std::string("Linear ") + component, "Colour", "float32", false, true});
        result.push_back({"/synesthesia/frame/colour/space", "Working colour space", "Colour", "string", false});
        return result;
    }();
    return catalogue;
}

OSCRouting defaultRouting() {
    OSCRouting result;
    for (const auto& route : outputCatalogue())
        result.emplace(route.address, OSCRouteSettings{route.builtIn, route.supportsSmoothing && route.address.find("/colour/") != std::string::npos});
    return result;
}

std::vector<OSCOutput> buildOutputs(const OSCFrameData& frame, const OSCRouting& routing, bool includeDisabled, bool includeSamples, bool smoothBandMagnitudes, bool includeBandFrequencies) {
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
        return includeDisabled || (setting != routing.end() && setting->second.enabled &&
            (includeBandFrequencies || address != kBandFrequenciesAddress));
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
    }, true);
    for (int component = -1; component < 4; ++component) {
        const auto address = bands + (component == -1 ? "magnitudes" : component == 3 ? "response_seconds" : component == 0 ? "cie_x" : component == 1 ? "cie_y" : "cie_z");
        if (!enabled(address)) continue;
        const auto setting = routing.find(address);
        const bool smoothing = component >= 0 && component < 3 && setting != routing.end() && setting->second.smoothing;
        const auto& contributions = smoothing || component == 3 ? frame.presentedContributions : frame.sourceContributions;
        addVector(address, [&] {
            if (component == -1 && smoothBandMagnitudes)
                return std::vector<float>(frame.bands.smoothedMagnitudes.begin(), frame.bands.smoothedMagnitudes.end());
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
        }, contributions != nullptr && (component != -1 || !smoothBandMagnitudes || frame.bands.available));
    }
    addVector(bands + "spectral_flux", [&] {
        return std::vector<float>(frame.bands.spectralFlux.begin(), frame.bands.spectralFlux.end());
    }, frame.bands.available);
    addVector(bands + "onset_detected", [&] {
        return std::vector<float>(frame.bands.onsetDetected.begin(), frame.bands.onsetDetected.end());
    }, frame.bands.available);
    addVector(bands + "levels_normalised", [&] {
        std::vector<float> values(ColourPresentation::contributionBandCount, 0.0f);
        if (smoothBandMagnitudes) std::copy(frame.bands.smoothedMagnitudes.begin(), frame.bands.smoothedMagnitudes.end(), values.begin());
        else if (frame.sourceContributions) {
            for (const auto& band : *frame.sourceContributions)
                if (band.band < values.size() && std::isfinite(band.magnitude)) values[band.band] += std::max(0.0f, band.magnitude);
        }
        for (auto& value : values)
            value = value > 0.0f && std::isfinite(value) ?
                std::clamp((20.0f * std::log10(value) - kBandLevelMinimumDb) / -kBandLevelMinimumDb, 0.0f, 1.0f) : 0.0f;
        return values;
    }, frame.sourceAvailable && frame.sourceContributions != nullptr && (!smoothBandMagnitudes || frame.bands.available));
    using BandColours = std::array<ColourCore::RGB, ColourPresentation::contributionBandCount>;
    std::array<std::optional<BandColours>, 2> bandColours;
    const std::array<const char*, 3> components{"r", "g", "b"};
    for (std::size_t component = 0; component < components.size(); ++component) {
        const auto address = bands + "linear_" + components[component];
        const auto setting = routing.find(address);
        const bool smoothing = setting != routing.end() && setting->second.smoothing;
        const auto& contributions = smoothing ? frame.presentedContributions : frame.sourceContributions;
        addVector(address, [&] {
            auto& colours = bandColours[smoothing ? 1 : 0];
            if (!colours) {
                colours.emplace();
                std::array<std::array<double, 3>, ColourPresentation::contributionBandCount> xyz{};
                if (contributions) {
                    for (const auto& band : *contributions) {
                        if (band.band >= xyz.size()) continue;
                        for (std::size_t c = 0; c < 3; ++c)
                            if (std::isfinite(band.xyz[c])) xyz[band.band][c] += static_cast<double>(band.xyz[c]);
                    }
                }
                for (std::size_t i = 0; i < xyz.size(); ++i) {
                    if (xyz[i][1] <= 0.0) continue;
                    auto& rgb = (*colours)[i];
                    ColourCore::XYZtoRGB(static_cast<float>(xyz[i][0] / xyz[i][1]), 1.0f,
                        static_cast<float>(xyz[i][2] / xyz[i][1]), rgb.r, rgb.g, rgb.b, frame.colourSpace, false, true);
                    const float peak = std::max({rgb.r, rgb.g, rgb.b});
                    if (peak > 0.0f && std::isfinite(peak)) {
                        rgb.r /= peak;
                        rgb.g /= peak;
                        rgb.b /= peak;
                    } else rgb = {};
                }
            }
            std::vector<float> values(colours->size());
            for (std::size_t i = 0; i < values.size(); ++i)
                values[i] = component == 0 ? (*colours)[i].r : component == 1 ? (*colours)[i].g : (*colours)[i].b;
            return values;
        }, contributions != nullptr && (smoothing ? frame.presentedAnalysis.available : frame.sourceAvailable));
    }
    if (enabled(bands + "frame_number"))
        result.push_back({bands + "frame_number", static_cast<int64_t>(frame.frameId), {}, false, frame.sourceAvailable && frame.sourceContributions != nullptr});
    for (std::size_t component = 0; component < components.size(); ++component) {
        const auto address = std::string("/synesthesia/frame/colour/linear_") + components[component];
        if (!enabled(address)) continue;
        const auto setting = routing.find(address);
        const bool smoothing = setting != routing.end() && setting->second.smoothing;
        const auto& rgb = smoothing ? frame.colour.linear : frame.sourceColour.linear;
        const bool available = smoothing ? frame.presentedAnalysis.available : frame.sourceAnalysis.available;
        const float value = component == 0 ? rgb.r : component == 1 ? rgb.g : rgb.b;
        result.push_back({address, available ? value : NAN, {}, false, available});
    }
    const std::string spaceAddress = "/synesthesia/frame/colour/space";
    if (enabled(spaceAddress)) {
        const char* space = frame.colourSpace == ColourCore::ColourSpace::SRGB ? "sRGB" :
            frame.colourSpace == ColourCore::ColourSpace::DisplayP3 ? "Display P3" : "Rec.2020";
        appendStringValue(result, spaceAddress.c_str(), space);
    }
    return result;
}

std::vector<OSCPacket> encodePackets(const OSCFrameData& frame, std::span<const OSCOutput> outputs,
                                    const OSCRouting& routing, const OSCEncodingOptions& options) {
    std::vector<OSCPacket> result;
    std::optional<osc::OutboundPacketStream> packet;
    visitPackets(frame, outputs, routing, options,
        [&](const FrameContext& context, std::size_t limit) {
            result.emplace_back().bytes.resize(limit);
            packet.emplace(result.back().bytes.data(), limit);
            *packet << osc::BeginBundleImmediate;
            appendContext(*packet, frame, context);
        },
        [&](const OSCOutput& output, std::string_view address, std::size_t offset, std::size_t count, bool available) {
            if (!output.vector) {
                appendScalar(*packet, output);
                return;
            }
            *packet << osc::BeginMessage(address.data()) << static_cast<osc::int32>(frameIndex(frame));
            for (std::size_t i = 0; i < count; ++i)
                *packet << (available && offset + i < output.samples.size() ? output.samples[offset + i] : 0.0f);
            *packet << osc::EndMessage;
        },
        [&] {
            *packet << osc::EndBundle;
            result.back().bytes.resize(packet->Size());
            packet.reset();
        });
    return result;
}

OSCOutputSize outputSize(const OSCFrameData& frame, std::span<const OSCOutput> outputs,
                         const OSCRouting& routing, const OSCEncodingOptions& options) {
    return visitPackets(frame, outputs, routing, options,
        [](const FrameContext&, std::size_t) {},
        [](const OSCOutput&, std::string_view, std::size_t, std::size_t, bool) {}, [] {});
}

OSCOutputSize outputSize(const OSCOutput& output, const OSCFrameData& frame) {
    if (!output.vector) return {scalarSize(output) + 4, 0, 0};
    const OSCRouting routing{{output.address, {true, false}}};
    return outputSize(frame, std::span<const OSCOutput>(&output, 1), routing);
}

}

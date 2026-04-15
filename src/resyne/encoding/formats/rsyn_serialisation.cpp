#include "resyne/encoding/formats/rsyn_serialisation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

namespace RSYNSerialisation {

namespace {

using json = nlohmann::json;

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
void appendIntegral(std::vector<std::uint8_t>& output, T value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);

    using Unsigned = typename UnsignedStorage<T>::type;
    const Unsigned encoded = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        output.push_back(static_cast<std::uint8_t>((encoded >> (index * 8U)) & 0xFFU));
    }
}

template <typename T>
bool readIntegral(const std::vector<std::uint8_t>& input, std::size_t& offset, T& value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);

    if (offset + sizeof(T) > input.size()) {
        return false;
    }

    using Unsigned = typename UnsignedStorage<T>::type;
    Unsigned decoded = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        decoded |= static_cast<Unsigned>(input[offset + index]) << (index * 8U);
    }
    value = static_cast<T>(decoded);
    offset += sizeof(T);
    return true;
}

template <typename T>
void appendFloat(std::vector<std::uint8_t>& output, T value) {
    static_assert(std::is_floating_point_v<T>);

    std::array<std::uint8_t, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    output.insert(output.end(), bytes.begin(), bytes.end());
}

template <typename T>
bool readFloat(const std::vector<std::uint8_t>& input, std::size_t& offset, T& value) {
    static_assert(std::is_floating_point_v<T>);

    if (offset + sizeof(T) > input.size()) {
        return false;
    }

    std::memcpy(&value, input.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

template <typename T>
void appendFloatVector(std::vector<std::uint8_t>& output, const std::vector<T>& values) {
    appendIntegral(output, static_cast<std::uint32_t>(values.size()));
    for (const T value : values) {
        appendFloat(output, value);
    }
}

template <typename T>
bool readFloatVector(const std::vector<std::uint8_t>& input, std::size_t& offset, std::vector<T>& values) {
    std::uint32_t size = 0;
    if (!readIntegral(input, offset, size)) {
        return false;
    }

    values.resize(size);
    for (T& value : values) {
        if (!readFloat(input, offset, value)) {
            return false;
        }
    }
    return true;
}

json encodePresentationSettings(const RSYNPresentationSettings& settings) {
    return json{
        {"colour_space", static_cast<std::uint32_t>(settings.colourSpace)},
        {"apply_gamut_mapping", settings.applyGamutMapping},
        {"low_gain", settings.lowGain},
        {"mid_gain", settings.midGain},
        {"high_gain", settings.highGain},
        {"smoothing_enabled", settings.smoothingEnabled},
        {"manual_smoothing", settings.manualSmoothing},
        {"smoothing_amount", settings.smoothingAmount},
        {"smoothing_update_factor", settings.smoothingUpdateFactor},
        {"spring_mass", settings.springMass},
        {"pipeline_id", settings.pipelineId}
    };
}

void decodePresentationSettings(const json& input, RSYNPresentationSettings& settings) {
    settings.colourSpace = static_cast<ColourCore::ColourSpace>(input.value("colour_space", static_cast<std::uint32_t>(settings.colourSpace)));
    settings.applyGamutMapping = input.value("apply_gamut_mapping", settings.applyGamutMapping);
    settings.lowGain = input.value("low_gain", settings.lowGain);
    settings.midGain = input.value("mid_gain", settings.midGain);
    settings.highGain = input.value("high_gain", settings.highGain);
    settings.smoothingEnabled = input.value("smoothing_enabled", settings.smoothingEnabled);
    settings.manualSmoothing = input.value("manual_smoothing", settings.manualSmoothing);
    settings.smoothingAmount = input.value("smoothing_amount", settings.smoothingAmount);
    settings.smoothingUpdateFactor = input.value("smoothing_update_factor", settings.smoothingUpdateFactor);
    settings.springMass = input.value("spring_mass", settings.springMass);
    settings.pipelineId = input.value("pipeline_id", settings.pipelineId);
}

void writeFrameResult(std::vector<std::uint8_t>& output, const ColourCore::FrameResult& result) {
    appendFloat(output, result.r);
    appendFloat(output, result.g);
    appendFloat(output, result.b);
    appendFloat(output, result.X);
    appendFloat(output, result.Y);
    appendFloat(output, result.Z);
    appendFloat(output, result.dominantWavelength);
    appendFloat(output, result.dominantFrequency);
    appendFloat(output, result.colourIntensity);
    appendFloat(output, result.L);
    appendFloat(output, result.a);
    appendFloat(output, result.b_comp);
    appendFloat(output, result.spectralCentroid);
    appendFloat(output, result.spectralFlatness);
    appendFloat(output, result.spectralSpread);
    appendFloat(output, result.spectralRolloff);
    appendFloat(output, result.spectralCrestFactor);
    appendFloat(output, result.loudnessDb);
    appendFloat(output, result.frameLoudnessDb);
    appendFloat(output, result.brightnessLoudnessDb);
    appendFloat(output, result.loudnessNormalised);
    appendFloat(output, result.brightnessNormalised);
    appendFloat(output, result.transientMix);
    appendFloat(output, result.estimatedSPL);
    appendFloat(output, result.luminanceCdM2);
    appendFloat(output, result.phaseInstabilityNorm);
    appendFloat(output, result.phaseCoherenceNorm);
    appendFloat(output, result.phaseTransientNorm);
}

bool readFrameResult(const std::vector<std::uint8_t>& input, std::size_t& offset, ColourCore::FrameResult& result) {
    return readFloat(input, offset, result.r) &&
        readFloat(input, offset, result.g) &&
        readFloat(input, offset, result.b) &&
        readFloat(input, offset, result.X) &&
        readFloat(input, offset, result.Y) &&
        readFloat(input, offset, result.Z) &&
        readFloat(input, offset, result.dominantWavelength) &&
        readFloat(input, offset, result.dominantFrequency) &&
        readFloat(input, offset, result.colourIntensity) &&
        readFloat(input, offset, result.L) &&
        readFloat(input, offset, result.a) &&
        readFloat(input, offset, result.b_comp) &&
        readFloat(input, offset, result.spectralCentroid) &&
        readFloat(input, offset, result.spectralFlatness) &&
        readFloat(input, offset, result.spectralSpread) &&
        readFloat(input, offset, result.spectralRolloff) &&
        readFloat(input, offset, result.spectralCrestFactor) &&
        readFloat(input, offset, result.loudnessDb) &&
        readFloat(input, offset, result.frameLoudnessDb) &&
        readFloat(input, offset, result.brightnessLoudnessDb) &&
        readFloat(input, offset, result.loudnessNormalised) &&
        readFloat(input, offset, result.brightnessNormalised) &&
        readFloat(input, offset, result.transientMix) &&
        readFloat(input, offset, result.estimatedSPL) &&
        readFloat(input, offset, result.luminanceCdM2) &&
        readFloat(input, offset, result.phaseInstabilityNorm) &&
        readFloat(input, offset, result.phaseCoherenceNorm) &&
        readFloat(input, offset, result.phaseTransientNorm);
}

void writeSignals(std::vector<std::uint8_t>& output, const RSYNSmoothingSignals& signals) {
    appendIntegral(output, static_cast<std::uint8_t>(signals.onsetDetected ? 1 : 0));
    appendFloat(output, signals.spectralFlux);
    appendFloat(output, signals.spectralFlatness);
    appendFloat(output, signals.loudnessNormalised);
    appendFloat(output, signals.brightnessNormalised);
    appendFloat(output, signals.spectralSpreadNorm);
    appendFloat(output, signals.spectralRolloffNorm);
    appendFloat(output, signals.spectralCrestNorm);
    appendFloat(output, signals.phaseInstabilityNorm);
    appendFloat(output, signals.phaseCoherenceNorm);
    appendFloat(output, signals.phaseTransientNorm);
}

bool readSignals(const std::vector<std::uint8_t>& input, std::size_t& offset, RSYNSmoothingSignals& signals) {
    std::uint8_t onset = 0;
    if (!readIntegral(input, offset, onset)) {
        return false;
    }

    signals.onsetDetected = onset != 0;
    return readFloat(input, offset, signals.spectralFlux) &&
        readFloat(input, offset, signals.spectralFlatness) &&
        readFloat(input, offset, signals.loudnessNormalised) &&
        readFloat(input, offset, signals.brightnessNormalised) &&
        readFloat(input, offset, signals.spectralSpreadNorm) &&
        readFloat(input, offset, signals.spectralRolloffNorm) &&
        readFloat(input, offset, signals.spectralCrestNorm) &&
        readFloat(input, offset, signals.phaseInstabilityNorm) &&
        readFloat(input, offset, signals.phaseCoherenceNorm) &&
        readFloat(input, offset, signals.phaseTransientNorm);
}

void writeFloatTriple(std::vector<std::uint8_t>& output, const std::array<float, 3>& values) {
    for (const float value : values) {
        appendFloat(output, value);
    }
}

bool readFloatTriple(const std::vector<std::uint8_t>& input, std::size_t& offset, std::array<float, 3>& values) {
    return readFloat(input, offset, values[0]) &&
        readFloat(input, offset, values[1]) &&
        readFloat(input, offset, values[2]);
}

}

bool encodeMetadata(const AudioMetadata& metadata, std::vector<std::uint8_t>& output) {
    json encoded{
        {"sample_rate", metadata.sampleRate},
        {"fft_size", metadata.fftSize},
        {"hop_size", metadata.hopSize},
        {"duration_seconds", metadata.durationSeconds},
        {"window_type", metadata.windowType},
        {"num_frames", metadata.numFrames},
        {"num_bins", metadata.numBins},
        {"channels", metadata.channels},
        {"version", metadata.version},
        {"has_source_data", metadata.sourceData != nullptr && !metadata.sourceData->bytes.empty()},
        {"has_presentation_data", metadata.presentationData != nullptr && !metadata.presentationData->frames.empty()}
    };

    if (metadata.sourceData != nullptr && !metadata.sourceData->bytes.empty()) {
        encoded["source"] = {
            {"filename", metadata.sourceData->filename},
            {"extension", metadata.sourceData->extension},
            {"crc32", metadata.sourceData->crc32},
            {"size", metadata.sourceData->bytes.size()}
        };
    }

    if (metadata.presentationData != nullptr && !metadata.presentationData->frames.empty()) {
        encoded["presentation"] = {
            {"settings", encodePresentationSettings(metadata.presentationData->settings)},
            {"frame_count", metadata.presentationData->frames.size()}
        };
    }

    output = json::to_cbor(encoded);
    return true;
}

bool decodeMetadata(const std::vector<std::uint8_t>& input, AudioMetadata& metadata) {
    const json decoded = json::from_cbor(input, true, false);
    if (decoded.is_discarded()) {
        return false;
    }

    metadata.sourceData.reset();
    metadata.presentationData.reset();
    metadata.sampleRate = decoded.value("sample_rate", metadata.sampleRate);
    metadata.fftSize = decoded.value("fft_size", metadata.fftSize);
    metadata.hopSize = decoded.value("hop_size", metadata.hopSize);
    metadata.durationSeconds = decoded.value("duration_seconds", metadata.durationSeconds);
    metadata.windowType = decoded.value("window_type", metadata.windowType);
    metadata.numFrames = decoded.value("num_frames", metadata.numFrames);
    metadata.numBins = decoded.value("num_bins", metadata.numBins);
    metadata.channels = decoded.value("channels", metadata.channels);
    metadata.version = decoded.value("version", metadata.version);

    if (decoded.value("has_source_data", false) && decoded.contains("source")) {
        metadata.sourceData = std::make_shared<RSYNSourceData>();
        metadata.sourceData->filename = decoded["source"].value("filename", std::string{});
        metadata.sourceData->extension = decoded["source"].value("extension", std::string{});
        metadata.sourceData->crc32 = decoded["source"].value("crc32", std::uint32_t{0});
    }

    if (decoded.value("has_presentation_data", false) && decoded.contains("presentation")) {
        metadata.presentationData = std::make_shared<RSYNPresentationData>();
        decodePresentationSettings(decoded["presentation"].value("settings", json::object()), metadata.presentationData->settings);
    }

    return true;
}

bool encodeSourceBytes(const std::shared_ptr<RSYNSourceData>& sourceData,
                       std::vector<std::uint8_t>& output) {
    if (sourceData == nullptr) {
        output.clear();
        return true;
    }

    output.assign(sourceData->bytes.begin(), sourceData->bytes.end());
    return true;
}

bool decodeSourceBytes(const std::vector<std::uint8_t>& input,
                       AudioMetadata& metadata) {
    if (metadata.sourceData == nullptr) {
        metadata.sourceData = std::make_shared<RSYNSourceData>();
    }

    metadata.sourceData->bytes.assign(input.begin(), input.end());
    return true;
}

bool encodeSamples(const std::vector<AudioColourSample>& samples,
                   std::vector<std::uint8_t>& output) {
    output.clear();
    appendIntegral(output, static_cast<std::uint32_t>(samples.size()));

    for (const AudioColourSample& sample : samples) {
        appendFloat(output, sample.timestamp);
        appendFloat(output, sample.sampleRate);
        appendFloat(output, sample.loudnessLUFS);
        appendFloat(output, sample.splDb);
        appendIntegral(output, sample.channels);

        appendIntegral(output, static_cast<std::uint32_t>(sample.magnitudes.size()));
        for (const auto& channel : sample.magnitudes) {
            appendFloatVector(output, channel);
        }

        appendIntegral(output, static_cast<std::uint32_t>(sample.phases.size()));
        for (const auto& channel : sample.phases) {
            appendFloatVector(output, channel);
        }

        appendIntegral(output, static_cast<std::uint32_t>(sample.frequencies.size()));
        for (const auto& channel : sample.frequencies) {
            appendFloatVector(output, channel);
        }
    }

    return true;
}

bool decodeSamples(const std::vector<std::uint8_t>& input,
                   std::vector<AudioColourSample>& samples,
                   const SequenceFrameCallback& onFrameDecoded,
                   const std::function<void(float)>& progress) {
    samples.clear();

    std::size_t offset = 0;
    std::uint32_t frameCount = 0;
    if (!readIntegral(input, offset, frameCount)) {
        return false;
    }

    samples.resize(frameCount);
    const std::size_t callbackStride = std::max<std::size_t>(1, frameCount / 200U);
    for (std::uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        AudioColourSample& sample = samples[frameIndex];
        if (!readFloat(input, offset, sample.timestamp) ||
            !readFloat(input, offset, sample.sampleRate) ||
            !readFloat(input, offset, sample.loudnessLUFS) ||
            !readFloat(input, offset, sample.splDb) ||
            !readIntegral(input, offset, sample.channels)) {
            return false;
        }

        std::uint32_t magnitudeChannels = 0;
        if (!readIntegral(input, offset, magnitudeChannels)) {
            return false;
        }
        sample.magnitudes.resize(magnitudeChannels);
        for (auto& channel : sample.magnitudes) {
            if (!readFloatVector(input, offset, channel)) {
                return false;
            }
        }

        std::uint32_t phaseChannels = 0;
        if (!readIntegral(input, offset, phaseChannels)) {
            return false;
        }
        sample.phases.resize(phaseChannels);
        for (auto& channel : sample.phases) {
            if (!readFloatVector(input, offset, channel)) {
                return false;
            }
        }

        std::uint32_t frequencyChannels = 0;
        if (!readIntegral(input, offset, frequencyChannels)) {
            return false;
        }
        sample.frequencies.resize(frequencyChannels);
        for (auto& channel : sample.frequencies) {
            if (!readFloatVector(input, offset, channel)) {
                return false;
            }
        }

        if (onFrameDecoded && (((frameIndex + 1U) % callbackStride) == 0U || frameIndex + 1U == frameCount)) {
            onFrameDecoded(samples, frameIndex + 1U);
        }

        if (progress) {
            progress(static_cast<float>(frameIndex + 1U) / static_cast<float>(std::max<std::uint32_t>(1U, frameCount)));
        }
    }

    return offset == input.size();
}

bool encodePresentationFrames(const std::shared_ptr<RSYNPresentationData>& presentationData,
                              std::vector<std::uint8_t>& output) {
    output.clear();
    if (presentationData == nullptr) {
        appendIntegral(output, static_cast<std::uint32_t>(0));
        return true;
    }

    appendIntegral(output, static_cast<std::uint32_t>(presentationData->frames.size()));
    for (const RSYNPresentationFrame& frame : presentationData->frames) {
        appendFloat(output, frame.timestamp);
        writeFrameResult(output, frame.analysis);
        writeSignals(output, frame.smoothingSignals);
        writeFloatTriple(output, frame.targetOklab);
        writeFloatTriple(output, frame.smoothedOklab);
        writeFloatTriple(output, frame.smoothedLab);
        writeFloatTriple(output, frame.smoothedDisplayRgb);
    }
    return true;
}

bool decodePresentationFrames(const std::vector<std::uint8_t>& input,
                              AudioMetadata& metadata,
                              const std::function<void(float)>& progress) {
    if (metadata.presentationData == nullptr) {
        metadata.presentationData = std::make_shared<RSYNPresentationData>();
    }

    metadata.presentationData->frames.clear();
    std::size_t offset = 0;
    std::uint32_t frameCount = 0;
    if (!readIntegral(input, offset, frameCount)) {
        return false;
    }

    metadata.presentationData->frames.resize(frameCount);
    for (std::uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        RSYNPresentationFrame& frame = metadata.presentationData->frames[frameIndex];
        if (!readFloat(input, offset, frame.timestamp) ||
            !readFrameResult(input, offset, frame.analysis) ||
            !readSignals(input, offset, frame.smoothingSignals) ||
            !readFloatTriple(input, offset, frame.targetOklab) ||
            !readFloatTriple(input, offset, frame.smoothedOklab) ||
            !readFloatTriple(input, offset, frame.smoothedLab) ||
            !readFloatTriple(input, offset, frame.smoothedDisplayRgb)) {
            return false;
        }

        if (progress) {
            progress(static_cast<float>(frameIndex + 1U) / static_cast<float>(std::max<std::uint32_t>(1U, frameCount)));
        }
    }

    return offset == input.size();
}

}

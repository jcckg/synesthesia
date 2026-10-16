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
#include "colour/colour_track.h"

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

    if (size > (input.size() - offset) / sizeof(T)) return false;
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
        {"automatic_response", "resolution-connected-envelope-tracking-exponential-v5"},
        {"manual_response", "connected-colour-motion-one-euro-v2"},
        {"spectral_presentation", "causal-bass-shape-short-mass-v1"},
        {"bass_window", "1024-hann-decimated-kaiser-8.6"},
        {"bass_crossover_hz", {150,300}},
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
    settings.pipelineId = input.value("pipeline_id", std::string{});
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
    appendIntegral(output, static_cast<std::uint8_t>(result.spectral.available));
    appendFloat(output, result.spectral.xyz.X);
    appendFloat(output, result.spectral.xyz.Y);
    appendFloat(output, result.spectral.xyz.Z);
    appendIntegral(output, static_cast<std::uint8_t>(result.loudnessIsLufs));
}

bool readFrameResult(const std::vector<std::uint8_t>& input, std::size_t& offset, ColourCore::FrameResult& result, bool legacy) {
    const bool valid = readFloat(input, offset, result.r) &&
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
    if (!valid) return false;
    std::uint8_t available = 0;
    if (!readIntegral(input, offset, available) || available > 1) return false;
    result.spectral.available = available != 0;
    const bool spectralValid = readFloat(input, offset, result.spectral.xyz.X) &&
        readFloat(input, offset, result.spectral.xyz.Y) && readFloat(input, offset, result.spectral.xyz.Z);
    // Legacy records contain a second observer triplet; consume it only to preserve record boundaries.
    if (legacy) {
        std::array<float,3> discarded{};
        for (auto& value : discarded) if (!readFloat(input, offset, value)) return false;
    }
    std::uint8_t lufs = 0;
    if (!spectralValid || !readIntegral(input, offset, lufs) || lufs > 1) return false;
    result.loudnessIsLufs = lufs != 0;
    return true;
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
        {"original_sample_count", metadata.originalSampleCount},
        {"analysis_leading_samples", metadata.analysisLeadingSamples},
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
            {"frame_count", metadata.presentationData->frames.size()},
            {"interpolation", "XYZ"},
            {"gamut_mapping", "soft-neutral-axis-linear-rgb-v1"},
            {"colour_smoothing", "shared-spectral-contributions-exponential-v1"},
            {"onset_detector", "local-log-frequency-trajectory-peak-v1"},
            {"measurement_observer", "CIE-2015-2-degree"},
            {"presentation_observer", "CIE-2015-2-degree-D65"},
            {"rgb_reference_model", "CIE-2015-BT2020-lines-D65-v1"},
            {"lab_reference_illuminant", "D65"}
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
    metadata.originalSampleCount = decoded.value("original_sample_count", std::uint64_t{0});
    metadata.analysisLeadingSamples = decoded.value("analysis_leading_samples", std::uint32_t{0});
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
        if (!readablePresentationPipeline(metadata.presentationData->settings.pipelineId)) return false;
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
    if (samples.size() >= 0x80000000u) return false;
    // The count's high bit marks the bass extension; frames without bass retain the legacy SPEC layout.
    const bool hasBass = std::any_of(samples.begin(),samples.end(),[](const auto& sample) { return !sample.bass.empty(); });
    appendIntegral(output, static_cast<std::uint32_t>(samples.size()) | (hasBass ? 0x80000000u : 0u));

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
        if (hasBass) {
            if (sample.bass.size() > sample.channels) return false;
            appendIntegral(output,static_cast<std::uint32_t>(sample.bass.size()));
            for (const auto& channel : sample.bass) {
                appendIntegral(output,static_cast<std::uint32_t>(channel != nullptr));
                if (!channel) continue;
                if (!std::isfinite(channel->sampleRate) || channel->sampleRate < 2000 || channel->sampleRate >= 4000 ||
                    !std::isfinite(channel->coverage) || channel->coverage < 0 || channel->coverage > 1 ||
                    channel->magnitudes.size() > 513) return false;
                for (float magnitude : channel->magnitudes) if (!std::isfinite(magnitude) || magnitude < 0) return false;
                appendFloat(output,channel->sampleRate);
                appendFloat(output,channel->coverage);
                appendFloatVector(output,channel->magnitudes);
            }
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

    const bool hasBass = (frameCount & 0x80000000u) != 0;
    frameCount &= 0x7fffffffu;
    constexpr std::size_t minimumSampleBytes = sizeof(double) + 3 * sizeof(float) + 4 * sizeof(std::uint32_t);
    if (frameCount > (input.size() - offset) / minimumSampleBytes) return false;
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
        if (magnitudeChannels > (input.size() - offset) / sizeof(std::uint32_t)) return false;
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
        if (phaseChannels > (input.size() - offset) / sizeof(std::uint32_t)) return false;
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
        if (frequencyChannels > (input.size() - offset) / sizeof(std::uint32_t)) return false;
        sample.frequencies.resize(frequencyChannels);
        for (auto& channel : sample.frequencies) {
            if (!readFloatVector(input, offset, channel)) {
                return false;
            }
        }

        if (hasBass) {
            std::uint32_t channels = 0;
            if (!readIntegral(input,offset,channels) || channels > sample.channels ||
                channels > (input.size()-offset)/sizeof(std::uint32_t)) return false;
            sample.bass.resize(channels);
            for (auto& channel : sample.bass) {
                std::uint32_t present = 0;
                if (!readIntegral(input,offset,present) || present > 1) return false;
                if (!present) continue;
                auto spectrum = std::make_shared<AudioAnalysis::BassSpectrum>();
                if (!readFloat(input,offset,spectrum->sampleRate) || !readFloat(input,offset,spectrum->coverage) ||
                    !readFloatVector(input,offset,spectrum->magnitudes)) return false;
                if (!std::isfinite(spectrum->sampleRate) || spectrum->sampleRate < 2000 || spectrum->sampleRate >= 4000 ||
                    !std::isfinite(spectrum->coverage) || spectrum->coverage < 0 || spectrum->coverage > 1 ||
                    spectrum->magnitudes.size() > 513) return false;
                for (float magnitude : spectrum->magnitudes) if (!std::isfinite(magnitude) || magnitude < 0) return false;
                channel = std::move(spectrum);
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

static bool validContributionFrame(const RSYNPresentationFrame& frame) {
    if (!std::isfinite(frame.timestamp) || !std::isfinite(frame.responseSeconds) || frame.responseSeconds < 0.0f ||
        !std::isfinite(frame.frequencyResolution) || frame.frequencyResolution < 0.0f) return false;
    const std::array<float, 3> raw{frame.analysis.X, frame.analysis.Y, frame.analysis.Z};
    for (const auto& pair : {std::pair{frame.contributionTargets, raw}, std::pair{frame.presentedContributions, frame.presentedXYZ}}) {
        if (!pair.first || pair.first->size() > ColourPresentation::contributionBandCount) return false;
        std::array<double, 3> sum{};
        std::uint16_t previous = 0;
        for (std::size_t i = 0; i < pair.first->size(); ++i) {
            const auto& band = (*pair.first)[i];
            if (band.band >= ColourPresentation::contributionBandCount || (i > 0 && band.band <= previous) ||
                !std::isfinite(band.magnitude) || band.magnitude < 0.0f ||
                !std::isfinite(band.responseSeconds) || band.responseSeconds < 0.0f) return false;
            for (std::size_t c = 0; c < 3; ++c) {
                if (!std::isfinite(band.xyz[c])) return false;
                sum[c] += static_cast<double>(band.xyz[c]);
            }
            previous = band.band;
        }
        for (std::size_t c = 0; c < 3; ++c) {
            const double expected = static_cast<double>(pair.second[c]);
            if (!std::isfinite(expected) || std::abs(sum[c] - expected) > 2e-6 * std::max(1.0, std::abs(expected))) return false;
        }
    }
    return true;
}

bool encodePresentationFrames(const std::shared_ptr<RSYNPresentationData>& presentationData,
                              std::vector<std::uint8_t>& output) {
    output.clear();
    if (presentationData && presentationData->settings.pipelineId != RSYN_PRESENTATION_PIPELINE_ID) return false;
    appendIntegral(output, std::numeric_limits<std::uint32_t>::max());
    appendIntegral(output, std::uint32_t{4});
    if (presentationData == nullptr) {
        appendIntegral(output, static_cast<std::uint32_t>(0));
        return true;
    }

    appendIntegral(output, static_cast<std::uint32_t>(presentationData->frames.size()));
    double previousTimestamp = -std::numeric_limits<double>::infinity();
    for (const RSYNPresentationFrame& frame : presentationData->frames) {
        if (!validContributionFrame(frame) || frame.timestamp <= previousTimestamp) return false;
        previousTimestamp = frame.timestamp;
        appendFloat(output, frame.timestamp);
        writeFrameResult(output, frame.analysis);
        writeSignals(output, frame.smoothingSignals);
        writeFloatTriple(output, frame.presentedXYZ);
        appendFloat(output, frame.responseSeconds);
        appendFloat(output, frame.frequencyResolution);
        for (const auto& bands : {frame.contributionTargets, frame.presentedContributions}) {
            appendIntegral(output, static_cast<std::uint32_t>(bands ? bands->size() : 0));
            if (!bands) continue;
            for (const auto& band : *bands) {
                appendIntegral(output, band.band);
                writeFloatTriple(output, band.xyz);
                appendFloat(output, band.magnitude);
                appendFloat(output, band.responseSeconds);
            }
        }
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

    if (frameCount != std::numeric_limits<std::uint32_t>::max()) return false;
    std::uint32_t version = 0;
    if (!readIntegral(input, offset, version) || (version != 3 && version != 4) || !readIntegral(input, offset, frameCount)) return false;
    if (!readablePresentationPipeline(metadata.presentationData->settings.pipelineId)) return false;
    const bool legacy = version == 3;
    if (legacy == (metadata.presentationData->settings.pipelineId == RSYN_PRESENTATION_PIPELINE_ID)) return false;
    const std::size_t minimumFrameBytes = legacy ? 215 : 203;
    if (frameCount > (input.size() - offset) / minimumFrameBytes) return false;

    metadata.presentationData->frames.resize(frameCount);
    for (std::uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        RSYNPresentationFrame& frame = metadata.presentationData->frames[frameIndex];
        if (!readFloat(input, offset, frame.timestamp) ||
            !readFrameResult(input, offset, frame.analysis, legacy) ||
            !readSignals(input, offset, frame.smoothingSignals)) {
            return false;
        }

        {
            if (!readFloatTriple(input, offset, frame.presentedXYZ) || !readFloat(input, offset, frame.responseSeconds) ||
                !std::isfinite(frame.responseSeconds) || frame.responseSeconds < 0.0f ||
                !readFloat(input, offset, frame.frequencyResolution) || !std::isfinite(frame.frequencyResolution) ||
                frame.frequencyResolution < 0.0f) return false;
            for (const float value : frame.presentedXYZ) if (!std::isfinite(value)) return false;
            for (auto* destination : {&frame.contributionTargets, &frame.presentedContributions}) {
                std::uint32_t count = 0;
                if (!readIntegral(input, offset, count) || count > ColourPresentation::contributionBandCount) return false;
                auto bands = std::make_shared<ColourPresentation::SpectralContributions>();
                bands->resize(count);
                std::uint16_t previous = 0;
                for (std::size_t i = 0; i < count; ++i) {
                    auto& band = (*bands)[i];
                    if (!readIntegral(input, offset, band.band) || band.band >= ColourPresentation::contributionBandCount ||
                        (i > 0 && band.band <= previous) || !readFloatTriple(input, offset, band.xyz) ||
                        !readFloat(input, offset, band.magnitude) || !readFloat(input, offset, band.responseSeconds)) return false;
                    for (const float value : band.xyz) if (!std::isfinite(value)) return false;
                    if (!std::isfinite(band.magnitude) || band.magnitude < 0.0f ||
                        !std::isfinite(band.responseSeconds) || band.responseSeconds < 0.0f) return false;
                    previous = band.band;
                }
                *destination = std::move(bands);
            }
        }

        // Older presentations are rebuilt from spectra by the loader; never reinterpret their cached XYZ.
        if (!legacy) {
            ColourCore::XYZtoOklab(frame.analysis.X, frame.analysis.Y, frame.analysis.Z,
                frame.targetOklab[0], frame.targetOklab[1], frame.targetOklab[2]);
            ColourCore::XYZtoOklab(frame.presentedXYZ[0], frame.presentedXYZ[1], frame.presentedXYZ[2],
                frame.smoothedOklab[0], frame.smoothedOklab[1], frame.smoothedOklab[2]);
            ColourCore::XYZtoLab(frame.presentedXYZ[0], frame.presentedXYZ[1], frame.presentedXYZ[2],
                frame.smoothedLab[0], frame.smoothedLab[1], frame.smoothedLab[2]);
            const auto rgb = ColourPresentation::projectXYZ(ColourPresentation::toColourTriple(frame.presentedXYZ),
                metadata.presentationData->settings.colourSpace, metadata.presentationData->settings.applyGamutMapping);
            frame.smoothedDisplayRgb = {rgb.r, rgb.g, rgb.b};
        }
        if (!validContributionFrame(frame) || (frameIndex > 0 && frame.timestamp <= metadata.presentationData->frames[frameIndex - 1].timestamp)) return false;
        if (progress) {
            progress(static_cast<float>(frameIndex + 1U) / static_cast<float>(std::max<std::uint32_t>(1U, frameCount)));
        }
    }

    return offset == input.size();
}

}

#include "resyne/recorder/import_helpers.h"
#include "resyne/recorder/recorder.h"
#include "resyne/decoding/audio_decoder.h"
#include "resyne/encoding/formats/exporter.h"
#include "resyne/recorder/embedded_source_utils.h"
#include "audio/analysis/fft/fft_processor.h"
#include "colour/colour_core.h"
#include "constants.h"
#include "miniz.h"
#undef crc32

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>

namespace ReSyne::ImportHelpers {

namespace {

bool sanitiseDecodedAudio(AudioDecoding::DecodedAudio& decoded) {
    bool changed = false;
    for (auto& channel : decoded.channelSamples) {
        for (float& sample : channel) {
            if (!std::isfinite(sample)) {
                sample = 0.0f;
                changed = true;
            }
        }
    }
    return changed;
}

std::vector<float> buildInterleavedAudio(const AudioDecoding::DecodedAudio& decoded) {
    if (decoded.channelSamples.empty()) {
        return {};
    }

    const size_t channelCount = decoded.channelSamples.size();
    const size_t frameCount = decoded.channelSamples.front().size();
    std::vector<float> interleaved(frameCount * channelCount, 0.0f);

    for (size_t channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
        const auto& channelSamples = decoded.channelSamples[channelIndex];
        const size_t safeFrameCount = std::min(frameCount, channelSamples.size());
        for (size_t frameIndex = 0; frameIndex < safeFrameCount; ++frameIndex) {
            interleaved[frameIndex * channelCount + channelIndex] = channelSamples[frameIndex];
        }
    }

    return interleaved;
}

bool readFileBytes(const std::string& filepath, std::vector<std::uint8_t>& bytes) {
    bytes.clear();

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    const std::streamsize size = file.tellg();
    if (size < 0) {
        return false;
    }

    file.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!file.good()) {
            bytes.clear();
            return false;
        }
    }

    return true;
}

std::shared_ptr<RSYNSourceData> buildSourceDataFromFile(const std::string& filepath) {
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(filepath, bytes)) {
        return nullptr;
    }

    auto sourceData = std::make_shared<RSYNSourceData>();
    const std::filesystem::path fsPath(filepath);
    sourceData->filename = fsPath.filename().string();
    sourceData->extension = fsPath.extension().string();
    sourceData->crc32 = bytes.empty()
        ? 0U
        : static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, bytes.data(), bytes.size()));
    sourceData->bytes = std::move(bytes);
    return sourceData;
}

}

bool importAudioFile(
    const std::string& filepath,
    int analysisHopSize,
    std::vector<AudioColourSample>& samples,
    AudioMetadata& metadata,
    std::string& errorMessage,
    const ProgressCallback& onProgress,
    const PreviewCallback& onPreview,
    std::vector<float>* playbackAudio,
    const std::size_t maxAnalysisFrames
) {
    if (onProgress) onProgress(0.1f);

    if (maxAnalysisFrames == 0) {
        errorMessage = "analysis frame limit must be greater than zero";
        return false;
    }

    AudioDecoding::DecodedAudio decoded;
    if (!AudioDecoding::decodeFile(filepath, decoded, errorMessage)) {
        return false;
    }

    if (onProgress) onProgress(0.2f);

    if (decoded.channelSamples.empty() || decoded.sampleRate == 0) {
        errorMessage = "empty audio";
        return false;
    }

    if (sanitiseDecodedAudio(decoded)) {
        std::cerr << "[Synesthesia] Replaced non-finite decoded audio samples with silence for " << filepath << '\n';
    }

    if (playbackAudio != nullptr) {
        *playbackAudio = buildInterleavedAudio(decoded);
    }

    const uint32_t numChannels = static_cast<uint32_t>(decoded.channelSamples.size());

    if (numChannels == 0) {
        errorMessage = "no channels";
        return false;
    }

    const int resolvedHopSize = std::clamp(analysisHopSize, 1, FFTProcessor::FFT_SIZE / 2);

    std::vector<std::unique_ptr<FFTProcessor>> processors;
    processors.reserve(numChannels);
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        processors.push_back(std::make_unique<FFTProcessor>());
    }
    for (auto& processor : processors) {
        processor->setHopSize(resolvedHopSize);
    }

    samples.clear();
    const size_t estimatedFrames = decoded.channelSamples[0].size() / static_cast<size_t>(resolvedHopSize) + 1;
    samples.reserve(std::min(estimatedFrames, maxAnalysisFrames));

    const float sampleRate = static_cast<float>(decoded.sampleRate);
    const size_t chunkSize = static_cast<size_t>(resolvedHopSize) * 8;
    size_t offset = 0;
    uint64_t frameIndex = 0;
    bool hitFrameLimit = false;

    metadata.sampleRate = sampleRate;
    metadata.fftSize = FFTProcessor::FFT_SIZE;
    metadata.hopSize = resolvedHopSize;
    metadata.originalSampleCount = decoded.channelSamples[0].size();
    metadata.analysisLeadingSamples = static_cast<std::uint32_t>(FFTProcessor::FFT_SIZE - resolvedHopSize);
    metadata.durationSeconds = decoded.channelSamples[0].empty()
        ? 0.0
        : static_cast<double>(decoded.channelSamples[0].size()) / static_cast<double>(decoded.sampleRate);
    metadata.windowType = "hann";
    metadata.numFrames = 0;
    metadata.numBins = static_cast<size_t>(FFTProcessor::FFT_SIZE / 2 + 1);
    metadata.channels = numChannels;
    metadata.sourceData.reset();
    metadata.presentationData.reset();
    metadata.lazyAsset.reset();

    if (onPreview) onPreview(samples);

    auto consumeFrames = [&](std::vector<std::vector<FFTProcessor::FFTFrame>>&& channelFrames) {
        if (channelFrames.empty() || channelFrames[0].empty()) {
            return;
        }

        size_t frameCount = channelFrames[0].size();
        for (uint32_t ch = 1; ch < numChannels && ch < channelFrames.size(); ++ch) {
            frameCount = std::min(frameCount, channelFrames[ch].size());
        }

        if (frameCount == 0) {
            return;
        }

        for (size_t f = 0; f < frameCount; ++f) {
            if (samples.size() >= maxAnalysisFrames) {
                hitFrameLimit = true;
                return;
            }
            AudioColourSample sample;
            sample.magnitudes.resize(numChannels);
            sample.phases.resize(numChannels);
            sample.bass.resize(numChannels);
            sample.kWeightedMagnitudes.resize(numChannels);
            sample.channels = numChannels;

            bool channelsAligned = true;
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                if (channelFrames[ch][f].frameCounter != channelFrames[0][f].frameCounter) {
                    channelsAligned = false;
                    break;
                }
                sample.magnitudes[ch] = std::move(channelFrames[ch][f].magnitudes);
                sample.phases[ch] = std::move(channelFrames[ch][f].phases);
                sample.bass[ch] = channelFrames[ch][f].bass;
                sample.kWeightedMagnitudes[ch] = std::move(channelFrames[ch][f].kWeightedMagnitudes);
                if (ch == 0) {
                    sample.sampleRate = channelFrames[ch][f].sampleRate;
                    sample.loudnessLUFS = channelFrames[ch][f].loudnessLUFS;
                    sample.splDb = std::numeric_limits<float>::quiet_NaN();
                }
            }

            if (!channelsAligned) {
                continue;
            }

            std::vector<float> channelLoudness(numChannels);
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                channelLoudness[ch] = channelFrames[ch][f].loudnessLUFS;
            }
            sample.loudnessLUFS = LoudnessMeter::combineChannelLoudness(channelLoudness);
            sample.splDb = std::numeric_limits<float>::quiet_NaN();

            sample.timestamp = (static_cast<double>(frameIndex) * static_cast<double>(resolvedHopSize)
                                + resolvedHopSize - FFTProcessor::FFT_SIZE * 0.5) /
                               static_cast<double>(decoded.sampleRate);
            samples.push_back(std::move(sample));
            ++frameIndex;
        }
    };

    while (offset < decoded.channelSamples[0].size()) {
        size_t chunk = std::min(chunkSize, decoded.channelSamples[0].size() - offset);

        std::vector<std::vector<FFTProcessor::FFTFrame>> channelFrames(numChannels);
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            processors[ch]->processBuffer(
                std::span<const float>(decoded.channelSamples[ch].data() + offset, chunk),
                sampleRate);
            channelFrames[ch] = processors[ch]->getBufferedFrames();
        }

        offset += chunk;
        consumeFrames(std::move(channelFrames));
        if (hitFrameLimit) {
            break;
        }

        const float processProgress = static_cast<float>(offset) / static_cast<float>(decoded.channelSamples[0].size());
        if (onProgress) onProgress(0.2f + (processProgress * 0.6f));

        if (onPreview) onPreview(samples);
    }

    std::vector<std::vector<FFTProcessor::FFTFrame>> channelFrames(numChannels);
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        channelFrames[ch] = processors[ch]->finish();
    }
    consumeFrames(std::move(channelFrames));
    if (hitFrameLimit) {
        samples.clear();
        metadata = AudioMetadata{};
        errorMessage =
            "analysis frame limit exceeded (" + std::to_string(maxAnalysisFrames) + "); "
            "split the audio file or raise the limit";
        return false;
    }

    if (samples.empty()) {
        errorMessage = "no analyser frames";
        return false;
    }

    metadata.numFrames = samples.size();
    if (onPreview) onPreview(samples);
    metadata.sourceData = buildSourceDataFromFile(filepath);

    return true;
}

bool importRsynFile(
    const std::string& filepath,
    ColourCore::ColourSpace colourSpace,
    bool applyGamutMapping,
    float fallbackSampleRate,
    float importLowGain,
    float importMidGain,
    float importHighGain,
    std::vector<AudioColourSample>& samples,
    AudioMetadata& metadata,
    std::string& errorMessage,
    const ProgressCallback& onProgress,
    const PreviewCallback& onPreview,
    std::vector<float>* playbackAudio
) {
    (void)colourSpace;
    (void)applyGamutMapping;
    (void)importLowGain;
    (void)importMidGain;
    (void)importHighGain;
    if (onProgress) onProgress(0.05f);

    ProgressCallback fileProgress;
    if (onProgress) {
        fileProgress = [onProgress](float value) {
            onProgress(0.05f + value * 0.55f);
        };
    }

    if (!SequenceExporter::loadFromRsynShell(filepath, metadata, fileProgress)) {
        errorMessage = "parse failure";
        return false;
    }

    if (onProgress) onProgress(0.6f);

    const float resolvedSampleRate = metadata.sampleRate > 0.0f ? metadata.sampleRate :
                                    (fallbackSampleRate > 0.0f ? fallbackSampleRate : 44100.0f);
    const int resolvedHopSize = metadata.hopSize > 0 ? metadata.hopSize : FFTProcessor::HOP_SIZE;

    metadata.sampleRate = resolvedSampleRate;
    metadata.hopSize = resolvedHopSize;
    if (metadata.fftSize == 0) {
        metadata.fftSize = FFTProcessor::FFT_SIZE;
    }
    if (metadata.windowType.empty()) {
        metadata.windowType = "hann";
    }

    samples.clear();
    if (onPreview) onPreview(samples);

    if (playbackAudio != nullptr) {
        playbackAudio->clear();
        if (SequenceExporter::hydrateRsynSource(metadata)) {
            EmbeddedSourceUtils::decodeEmbeddedSourceAudio(metadata, *playbackAudio, errorMessage);
            errorMessage.clear();
        }
        if (!SequenceExporter::hydrateRsynSamples(metadata, samples,
            [onProgress](float progress) { if (onProgress) onProgress(0.6f + progress * 0.2f); })) {
            errorMessage = "archived audio frames could not be loaded";
            return false;
        }
        if (onPreview) onPreview(samples);
    }

    if (metadata.presentationData != nullptr && !metadata.presentationData->frames.empty()) {
        metadata.numFrames = metadata.presentationData->frames.size();
    }

    if (onProgress) onProgress(0.9f);
    return true;
}

}

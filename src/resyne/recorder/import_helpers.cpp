#include "resyne/recorder/import_helpers.h"
#include "resyne/recorder/recorder.h"
#include "resyne/decoding/audio_decoder.h"
#include "resyne/encoding/formats/exporter.h"
#include "audio/analysis/fft/fft_processor.h"
#include "audio/analysis/eq/equaliser.h"
#include "colour/colour_mapper.h"
#include "constants.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <span>

namespace ReSyne::ImportHelpers {

namespace {

}

bool importAudioFile(
    const std::string& filepath,
    float gamma,
    ColourMapper::ColourSpace colourSpace,
    bool applyGamutMapping,
    float importLowGain,
    float importMidGain,
    float importHighGain,
    std::vector<AudioColourSample>& samples,
    AudioMetadata& metadata,
    std::string& errorMessage,
    const ProgressCallback& onProgress,
    const PreviewCallback& onPreview
) {
    (void)gamma;
    (void)colourSpace;
    (void)applyGamutMapping;
    if (onProgress) onProgress(0.1f);

    AudioDecoding::DecodedAudio decoded;
    if (!AudioDecoding::decodeFile(filepath, decoded, errorMessage)) {
        return false;
    }

    if (onProgress) onProgress(0.2f);

    if (decoded.channelSamples.empty() || decoded.sampleRate == 0) {
        errorMessage = "empty audio";
        return false;
    }

    const uint32_t numChannels = static_cast<uint32_t>(decoded.channelSamples.size());

    if (numChannels == 0) {
        errorMessage = "no channels";
        return false;
    }

    std::vector<FFTProcessor> processors(numChannels);
    for (auto& processor : processors) {
        processor.setEQGains(importLowGain, importMidGain, importHighGain);
    }

    Equaliser equaliser;
    equaliser.setGains(importLowGain, importMidGain, importHighGain);
    samples.clear();
    samples.reserve(decoded.channelSamples[0].size() / FFTProcessor::HOP_SIZE + 1);

    const float sampleRate = static_cast<float>(decoded.sampleRate);
    const size_t chunkSize = static_cast<size_t>(FFTProcessor::HOP_SIZE) * 8;
    size_t offset = 0;
    uint64_t frameIndex = 0;

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
            AudioColourSample sample;
            sample.magnitudes.resize(numChannels);
            sample.phases.resize(numChannels);
            sample.channels = numChannels;

            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                sample.magnitudes[ch] = std::move(channelFrames[ch][f].magnitudes);
                sample.phases[ch] = std::move(channelFrames[ch][f].phases);
                if (ch == 0) {
                    sample.sampleRate = channelFrames[ch][f].sampleRate;
                    sample.loudnessLUFS = channelFrames[ch][f].loudnessLUFS;
                    sample.splDb = channelFrames[ch][f].loudnessLUFS + synesthesia::constants::REFERENCE_SPL_AT_0_LUFS;
                }
            }

            sample.timestamp = static_cast<double>(frameIndex * FFTProcessor::HOP_SIZE) /
                               static_cast<double>(decoded.sampleRate);
            samples.push_back(std::move(sample));
            ++frameIndex;
            if (samples.size() >= RecorderState::MAX_SAMPLES) {
                break;
            }
        }
    };

    while (offset < decoded.channelSamples[0].size() &&
           samples.size() < RecorderState::MAX_SAMPLES) {
        size_t chunk = std::min(chunkSize, decoded.channelSamples[0].size() - offset);

        std::vector<std::vector<FFTProcessor::FFTFrame>> channelFrames(numChannels);
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            processors[ch].processBuffer(
                std::span<const float>(decoded.channelSamples[ch].data() + offset, chunk),
                sampleRate);
            channelFrames[ch] = processors[ch].getBufferedFrames();
        }

        offset += chunk;
        consumeFrames(std::move(channelFrames));

        const float processProgress = static_cast<float>(offset) / static_cast<float>(decoded.channelSamples[0].size());
        if (onProgress) onProgress(0.2f + (processProgress * 0.6f));

        if (onPreview && (samples.size() % 500 < chunkSize / FFTProcessor::HOP_SIZE ||
            offset >= decoded.channelSamples[0].size())) {
            onPreview(samples);
        }
    }

    if (samples.size() < RecorderState::MAX_SAMPLES) {
        std::vector<std::vector<FFTProcessor::FFTFrame>> channelFrames(numChannels);
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            channelFrames[ch] = processors[ch].getBufferedFrames();
        }
        consumeFrames(std::move(channelFrames));
    }

    if (samples.empty()) {
        errorMessage = "no analyser frames";
        return false;
    }

    size_t firstValidFrame = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
        if (samples[i].loudnessLUFS > -200.0f) {
            firstValidFrame = i;
            break;
        }
    }

    const double overlapBufferOffsetSeconds = static_cast<double>(FFTProcessor::HOP_SIZE) /
                                               static_cast<double>(decoded.sampleRate);

    if (firstValidFrame > 0) {
        samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(firstValidFrame));
    }

    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i].timestamp -= static_cast<double>(firstValidFrame * FFTProcessor::HOP_SIZE) /
                                 static_cast<double>(decoded.sampleRate);
        samples[i].timestamp -= overlapBufferOffsetSeconds;
        if (samples[i].timestamp < 0.0) {
            samples[i].timestamp = 0.0;
        }
    }

    metadata.sampleRate = sampleRate;
    metadata.fftSize = FFTProcessor::FFT_SIZE;
    metadata.hopSize = FFTProcessor::HOP_SIZE;
    metadata.windowType = "hann";
    metadata.numFrames = samples.size();
    metadata.numBins = static_cast<size_t>(FFTProcessor::FFT_SIZE / 2 + 1);
    metadata.channels = numChannels;
    metadata.version = "3.0.0";

    return true;
}

bool importResyneFile(
    const std::string& filepath,
    float gamma,
    ColourMapper::ColourSpace colourSpace,
    bool applyGamutMapping,
    float fallbackSampleRate,
    float importLowGain,
    float importMidGain,
    float importHighGain,
    std::vector<AudioColourSample>& samples,
    AudioMetadata& metadata,
    std::string& errorMessage,
    const ProgressCallback& onProgress,
    const PreviewCallback& onPreview
) {
    (void)gamma;
    (void)colourSpace;
    (void)applyGamutMapping;
    if (onProgress) onProgress(0.05f);

    ProgressCallback fileProgress;
    if (onProgress) {
        fileProgress = [onProgress](float value) {
            onProgress(0.05f + value * 0.55f);
        };
    }

    SequenceFrameCallback frameCallback;
    if (onPreview) {
        frameCallback = [onPreview](const std::vector<AudioColourSample>& decoded, size_t validCount) {
            if (validCount == 0 || decoded.empty()) {
                return;
            }
            const size_t safeCount = std::min(validCount, decoded.size());
            std::vector<AudioColourSample> preview(decoded.begin(), decoded.begin() + static_cast<std::vector<AudioColourSample>::difference_type>(safeCount));
            onPreview(preview);
        };
    }

    if (!SequenceExporter::loadFromResyne(filepath, samples, metadata, fileProgress, frameCallback) || samples.empty()) {
        errorMessage = "parse failure";
        return false;
    }

    if (onProgress) onProgress(0.6f);

    const float resolvedSampleRate = metadata.sampleRate > 0.0f ? metadata.sampleRate :
                                    (fallbackSampleRate > 0.0f ? fallbackSampleRate : 44100.0f);
    const int resolvedHopSize = metadata.hopSize > 0 ? metadata.hopSize : FFTProcessor::HOP_SIZE;

    metadata.sampleRate = resolvedSampleRate;
    metadata.hopSize = resolvedHopSize;
    metadata.numFrames = samples.size();
    if (metadata.fftSize == 0) {
        metadata.fftSize = FFTProcessor::FFT_SIZE;
    }
    if (metadata.numBins == 0 && !samples.empty() && !samples.front().magnitudes.empty()) {
        metadata.numBins = samples.front().magnitudes[0].size();
    }
    if (metadata.windowType.empty()) {
        metadata.windowType = "hann";
    }

    Equaliser equaliser;
    equaliser.setGains(importLowGain, importMidGain, importHighGain);

    const double hopSizeAsDouble = static_cast<double>(resolvedHopSize);
    const size_t totalFrames = samples.size();
    for (size_t frame = 0; frame < samples.size(); ++frame) {
        auto& sample = samples[frame];
        const float frameSampleRate = sample.sampleRate > 0.0f ? sample.sampleRate : resolvedSampleRate;
        sample.sampleRate = frameSampleRate;
        sample.timestamp = (hopSizeAsDouble * static_cast<double>(frame)) /
                           static_cast<double>(std::max(resolvedSampleRate, 1e-6f));

        if (frame % 100 == 0 || frame == totalFrames - 1) {
            const float processProgress = static_cast<float>(frame) / static_cast<float>(totalFrames);
            if (onProgress) onProgress(0.6f + (processProgress * 0.25f));
        }

        if (onPreview && (frame % 500 == 0 || frame == totalFrames - 1)) {
            onPreview(samples);
        }
    }

    if (onProgress) onProgress(0.9f);
    return true;
}

}

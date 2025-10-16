#include "resyne/recorder/import_helpers.h"
#include "resyne/recorder/recorder.h"
#include "resyne/decoding/audio_decoder.h"
#include "resyne/encoding/formats/exporter.h"
#include "audio/analysis/fft/fft_processor.h"
#include "audio/analysis/eq/equaliser.h"
#include "colour/colour_mapper.h"

#include <algorithm>
#include <cmath>
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

    if (decoded.samples.empty() || decoded.sampleRate == 0) {
        errorMessage = "empty audio";
        return false;
    }

    FFTProcessor processor;
    processor.setEQGains(importLowGain, importMidGain, importHighGain);
    Equaliser equaliser;
    equaliser.setGains(importLowGain, importMidGain, importHighGain);
    samples.clear();
    samples.reserve(decoded.samples.size() / FFTProcessor::HOP_SIZE + 1);

    const float sampleRate = static_cast<float>(decoded.sampleRate);
    const size_t chunkSize = static_cast<size_t>(FFTProcessor::HOP_SIZE) * 8;
    size_t offset = 0;
    uint64_t frameIndex = 0;

    auto consumeFrames = [&](std::vector<FFTProcessor::FFTFrame>&& frames) {
        for (auto& frame : frames) {
            AudioColourSample sample;
            sample.magnitudes = std::move(frame.magnitudes);
            sample.phases = std::move(frame.phases);
            sample.sampleRate = frame.sampleRate;
            sample.timestamp = static_cast<double>(frameIndex * FFTProcessor::HOP_SIZE) /
                               static_cast<double>(decoded.sampleRate);
            samples.push_back(std::move(sample));
            ++frameIndex;
            if (samples.size() >= RecorderState::MAX_SAMPLES) {
                break;
            }
        }
    };

    while (offset < decoded.samples.size() &&
           samples.size() < RecorderState::MAX_SAMPLES) {
        size_t chunk = std::min(chunkSize, decoded.samples.size() - offset);
        processor.processBuffer(std::span<const float>(decoded.samples.data() + offset, chunk), sampleRate);
        offset += chunk;
        consumeFrames(processor.getBufferedFrames());

        const float processProgress = static_cast<float>(offset) / static_cast<float>(decoded.samples.size());
        if (onProgress) onProgress(0.2f + (processProgress * 0.6f));

        if (onPreview && (samples.size() % 500 < chunkSize / FFTProcessor::HOP_SIZE ||
            offset >= decoded.samples.size())) {
            onPreview(samples);
        }
    }

    if (samples.size() < RecorderState::MAX_SAMPLES) {
        consumeFrames(processor.getBufferedFrames());
    }

    if (samples.empty()) {
        errorMessage = "no analyser frames";
        return false;
    }

    metadata.sampleRate = sampleRate;
    metadata.fftSize = FFTProcessor::FFT_SIZE;
    metadata.hopSize = FFTProcessor::HOP_SIZE;
    metadata.windowType = "hann";
    metadata.numFrames = samples.size();
    metadata.numBins = static_cast<size_t>(FFTProcessor::FFT_SIZE / 2 + 1);
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
            std::vector<AudioColourSample> preview(decoded.begin(),
                                                   decoded.begin() + static_cast<std::vector<AudioColourSample>::difference_type>(safeCount));
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
    if (metadata.numBins == 0 && !samples.empty()) {
        metadata.numBins = samples.front().magnitudes.size();
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

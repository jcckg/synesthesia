#include "resyne/recorder/reconstruction_utils.h"

#include <algorithm>

#include "resyne/encoding/audio/wav_encoder.h"

namespace ReSyne::RecorderReconstruction {

bool buildPlaybackAudio(const std::vector<AudioColourSample>& samples,
                        const AudioMetadata& metadata,
                        std::vector<float>& playbackAudio,
                        const ProgressCallback& onProgress) {
    playbackAudio.clear();
    if (samples.empty() || metadata.sampleRate <= 0.0f || metadata.fftSize <= 0 || metadata.hopSize <= 0) {
        return false;
    }

    const uint32_t numChannels = samples.front().channels > 0 ? samples.front().channels : 1;
    std::vector<std::vector<float>> channelAudioData(numChannels);
    size_t maxLength = 0;

    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        std::vector<SpectralSample> spectralSamples;
        spectralSamples.reserve(samples.size());

        for (const auto& sample : samples) {
            SpectralSample spectral;
            if (ch < sample.magnitudes.size()) {
                spectral.magnitudes.push_back(sample.magnitudes[ch]);
            } else {
                spectral.magnitudes.emplace_back();
            }
            if (ch < sample.phases.size()) {
                spectral.phases.push_back(sample.phases[ch]);
            } else {
                spectral.phases.emplace_back();
            }
            if (ch < sample.frequencies.size()) {
                spectral.frequencies.push_back(sample.frequencies[ch]);
            } else {
                spectral.frequencies.emplace_back();
            }
            spectral.timestamp = sample.timestamp;
            spectral.sampleRate = sample.sampleRate;
            spectralSamples.push_back(std::move(spectral));
        }

        auto result = WAVEncoder::reconstructFromSpectralData(
            spectralSamples,
            metadata.sampleRate,
            metadata.fftSize,
            metadata.hopSize);

        if (!result.success || result.audioSamples.empty()) {
            playbackAudio.clear();
            return false;
        }

        channelAudioData[ch] = std::move(result.audioSamples);
        maxLength = std::max(maxLength, channelAudioData[ch].size());
        if (onProgress) {
            onProgress(static_cast<float>(ch + 1) / static_cast<float>(numChannels));
        }
    }

    playbackAudio.reserve(maxLength * numChannels);
    for (size_t i = 0; i < maxLength; ++i) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            playbackAudio.push_back(i < channelAudioData[ch].size() ? channelAudioData[ch][i] : 0.0f);
        }
    }

    return !playbackAudio.empty();
}

}

#include "resyne/recorder/reconstruction_utils.h"


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

    std::vector<SpectralSample> spectra;
    spectra.reserve(samples.size());
    for (const auto& sample : samples)
        spectra.push_back({sample.magnitudes, sample.phases, sample.frequencies, sample.timestamp, sample.sampleRate});
    auto result = WAVEncoder::reconstructFromSpectralData(spectra, metadata.sampleRate,
        metadata.fftSize, metadata.hopSize, metadata.analysisLeadingSamples, metadata.originalSampleCount);
    if (!result.success) return false;
    playbackAudio = std::move(result.audioSamples);
    if (onProgress) onProgress(1.0f);
    return !playbackAudio.empty();
}

}

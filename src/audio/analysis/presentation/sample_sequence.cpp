#include "audio/analysis/presentation/sample_sequence.h"

#include <cmath>
#include <algorithm>

namespace SpectralPresentation::SampleSequence {

Frame buildFrame(const AudioColourSample& sample) {
    auto frame = SpectralPresentation::mixChannels(
        sample.magnitudes,
        sample.phases,
        sample.frequencies,
        sample.channels,
        sample.sampleRate,
        sample.kWeightedMagnitudes);
    const auto channels = static_cast<std::size_t>(sample.channels);
    if (channels > 0 && sample.bass.size() == channels && sample.bass.front()) {
        auto mixed = std::make_shared<AudioAnalysis::BassSpectrum>(*sample.bass.front());
        std::fill(mixed->magnitudes.begin(),mixed->magnitudes.end(),0);
        for (const auto& channel : sample.bass) {
            if (!channel || channel->sampleRate != mixed->sampleRate || channel->magnitudes.size() != mixed->magnitudes.size()) return frame;
            mixed->coverage = std::min(mixed->coverage,channel->coverage);
            for (std::size_t i = 0; i < mixed->magnitudes.size(); ++i)
                mixed->magnitudes[i] += channel->magnitudes[i]*channel->magnitudes[i]/static_cast<float>(channels);
        }
        for (auto& value : mixed->magnitudes) value = std::sqrt(value);
        frame.bass = std::move(mixed);
    }
    return frame;
}

float resolveDeltaTimeSeconds(const AudioColourSample* previousSample,
                              const AudioColourSample& currentSample,
                              const float fallbackDeltaTimeSeconds) {
    if (previousSample != nullptr) {
        return static_cast<float>(currentSample.timestamp - previousSample->timestamp);
    }

    return fallbackDeltaTimeSeconds;
}

PreparedFrame prepareSampleFrame(const AudioColourSample& sample,
                                 const Settings& settings,
                                 const AudioColourSample* previousSample,
                                 const float fallbackDeltaTimeSeconds) {
    const float loudnessOverride = std::isfinite(sample.loudnessLUFS)
        ? sample.loudnessLUFS
        : ColourCore::LOUDNESS_DB_UNSPECIFIED;
    const Frame frame = buildFrame(sample);

    Frame previousFrame{};
    const Frame* previousFramePtr = nullptr;
    if (previousSample != nullptr) {
        previousFrame = buildFrame(*previousSample);
        previousFramePtr = &previousFrame;
    }

    return SpectralPresentation::prepareFrame(
        frame,
        settings,
        loudnessOverride,
        previousFramePtr,
        resolveDeltaTimeSeconds(previousSample, sample, fallbackDeltaTimeSeconds));
}

}

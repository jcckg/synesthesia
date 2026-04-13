#include "audio/analysis/presentation/sample_sequence.h"

#include <cmath>

namespace SpectralPresentation::SampleSequence {

Frame buildFrame(const AudioColourSample& sample) {
    return SpectralPresentation::mixChannels(
        sample.magnitudes,
        sample.phases,
        sample.frequencies,
        sample.channels,
        sample.sampleRate);
}

float resolveDeltaTimeSeconds(const AudioColourSample* previousSample,
                              const AudioColourSample& currentSample,
                              const float fallbackDeltaTimeSeconds) {
    if (previousSample != nullptr) {
        const double delta = currentSample.timestamp - previousSample->timestamp;
        if (std::isfinite(delta) && delta > 0.0) {
            return static_cast<float>(delta);
        }
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

ColourCore::FrameResult buildSampleColourResult(const AudioColourSample& sample,
                                                const Settings& settings,
                                                const AudioColourSample* previousSample,
                                                const float fallbackDeltaTimeSeconds) {
    return prepareSampleFrame(sample, settings, previousSample, fallbackDeltaTimeSeconds).colourResult;
}

}

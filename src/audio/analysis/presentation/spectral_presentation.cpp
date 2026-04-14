#include "audio/analysis/presentation/spectral_presentation.h"

#include <algorithm>
#include <cmath>

#include "audio/analysis/eq/shared_eq_model.h"
#include "audio/analysis/fft/fft_processor.h"
#include "audio/analysis/phase/phase_features.h"

namespace SpectralPresentation {

namespace {

constexpr float kMinimumMagnitude = 1e-6f;
constexpr float kMinimumLoudnessDb = -70.0f;
constexpr float kMaximumLoudnessDb = 0.0f;

void sanitiseMagnitudes(std::vector<float>& magnitudes) {
    for (float& magnitude : magnitudes) {
        if (!std::isfinite(magnitude) || magnitude < 0.0f) {
            magnitude = 0.0f;
        }
    }
}

size_t resolveFftSize(const size_t binCount) {
    if (binCount > 1) {
        return (binCount - 1) * 2;
    }

    return FFTProcessor::FFT_SIZE;
}

float resolveSpectrumPresence(const ColourCore::FrameResult& colourResult) {
    const float clampedLoudnessDb = std::clamp(
        colourResult.brightnessLoudnessDb,
        kMinimumLoudnessDb,
        kMaximumLoudnessDb);
    const float normalisedLoudness =
        (clampedLoudnessDb - kMinimumLoudnessDb) /
        (kMaximumLoudnessDb - kMinimumLoudnessDb);
    return std::pow(normalisedLoudness, 1.25f);
}

}

Frame mixChannels(const std::vector<std::vector<float>>& magnitudes,
                  const std::vector<std::vector<float>>& phases,
                  const std::vector<std::vector<float>>& frequencies,
                  const std::uint32_t channels,
                  const float sampleRate) {
    Frame frame{};
    frame.sampleRate = sampleRate;

    if (magnitudes.empty() || magnitudes.front().empty()) {
        return frame;
    }

    const size_t numBins = magnitudes.front().size();
    frame.magnitudes.assign(numBins, 0.0f);
    frame.phases.assign(numBins, 0.0f);

    size_t contributingMagnitudeChannels = 0;
    size_t contributingPhaseChannels = 0;
    const size_t channelLimit = std::min(magnitudes.size(), static_cast<size_t>(std::max<std::uint32_t>(1, channels)));

    for (size_t channelIndex = 0; channelIndex < channelLimit; ++channelIndex) {
        const auto& channelMagnitudes = magnitudes[channelIndex];
        if (channelMagnitudes.size() == numBins) {
            for (size_t bin = 0; bin < numBins; ++bin) {
                const float magnitude = channelMagnitudes[bin];
                frame.magnitudes[bin] += magnitude * magnitude;
            }
            ++contributingMagnitudeChannels;
        }

        if (channelIndex < phases.size()) {
            const auto& channelPhases = phases[channelIndex];
            if (channelPhases.size() == numBins) {
                for (size_t bin = 0; bin < numBins; ++bin) {
                    frame.phases[bin] += channelPhases[bin];
                }
                ++contributingPhaseChannels;
            }
        }
    }

    if (contributingMagnitudeChannels > 0) {
        const float invChannelCount = 1.0f / static_cast<float>(contributingMagnitudeChannels);
        for (float& magnitude : frame.magnitudes) {
            magnitude = std::sqrt(magnitude * invChannelCount);
        }
    }

    if (contributingPhaseChannels > 0) {
        const float invChannelCount = 1.0f / static_cast<float>(contributingPhaseChannels);
        for (float& phase : frame.phases) {
            phase *= invChannelCount;
        }
    }

    if (!frequencies.empty() && !frequencies.front().empty() && frequencies.front().size() == numBins) {
        frame.frequencies = frequencies.front();
    }

    sanitiseMagnitudes(frame.magnitudes);
    return frame;
}

std::vector<float> buildSharedMagnitudes(const Frame& frame,
                                         const Settings& settings) {
    std::vector<float> magnitudes = frame.magnitudes;
    if (magnitudes.empty() || frame.sampleRate <= 0.0f) {
        return magnitudes;
    }

    sanitiseMagnitudes(magnitudes);
    AudioEQ::applyMagnitudeResponse(
        magnitudes,
        frame.sampleRate,
        resolveFftSize(magnitudes.size()),
        settings.lowGain,
        settings.midGain,
        settings.highGain);

    sanitiseMagnitudes(magnitudes);
    return magnitudes;
}

std::vector<float> buildVisualiserMagnitudes(const std::vector<float>& sharedMagnitudes,
                                             const float sampleRate,
                                             const ColourCore::FrameResult& colourResult) {
    std::vector<float> magnitudes = sharedMagnitudes;
    if (magnitudes.empty() || sampleRate <= 0.0f) {
        return magnitudes;
    }

    const float presence = resolveSpectrumPresence(colourResult);
    float maxMagnitude = 0.0f;

    for (size_t index = 0; index < magnitudes.size(); ++index) {
        const float frequency =
            static_cast<float>(index) * sampleRate / static_cast<float>(resolveFftSize(magnitudes.size()));
        if (frequency < synesthesia::constants::MIN_AUDIO_FREQ ||
            frequency > synesthesia::constants::MAX_AUDIO_FREQ ||
            !std::isfinite(magnitudes[index])) {
            magnitudes[index] = 0.0f;
            continue;
        }

        maxMagnitude = std::max(maxMagnitude, magnitudes[index]);
    }

    if (maxMagnitude <= kMinimumMagnitude) {
        std::fill(magnitudes.begin(), magnitudes.end(), 0.0f);
        return magnitudes;
    }

    const float normalisation = presence / maxMagnitude;
    for (float& magnitude : magnitudes) {
        magnitude = std::clamp(magnitude * normalisation, 0.0f, 1.0f);
    }

    return magnitudes;
}

PreparedFrame prepareFrame(const Frame& frame,
                           const Settings& settings,
                           const float loudnessDb,
                           const PhaseAnalysis::PhaseFeatureMetrics& phaseMetrics) {
    PreparedFrame prepared{};
    const std::vector<float> sharedMagnitudes = buildSharedMagnitudes(frame, settings);
    prepared.colourResult = ColourCore::analyseSpectrum(
        sharedMagnitudes,
        frame.phases,
        frame.frequencies,
        frame.sampleRate,
        ColourCore::OutputSettings{
            .colourSpace = settings.colourSpace,
            .applyGamutMapping = settings.applyGamutMapping
        },
        loudnessDb,
        &phaseMetrics);
    prepared.visualiserMagnitudes = buildVisualiserMagnitudes(
        sharedMagnitudes,
        frame.sampleRate,
        prepared.colourResult);
    return prepared;
}

PreparedFrame prepareFrame(const Frame& frame,
                           const Settings& settings,
                           const float loudnessDb,
                           const Frame* previousFrame,
                           const float deltaTimeSeconds) {
    const PhaseAnalysis::PhaseFeatureMetrics phaseMetrics =
        previousFrame != nullptr
            ? PhaseAnalysis::analyseTransition(previousFrame, frame, deltaTimeSeconds)
            : PhaseAnalysis::PhaseFeatureMetrics{};
    return prepareFrame(frame, settings, loudnessDb, phaseMetrics);
}

PreparedFrame prepareFrame(const Frame& frame,
                           const Settings& settings,
                           const float loudnessDb) {
    return prepareFrame(frame, settings, loudnessDb, PhaseAnalysis::PhaseFeatureMetrics{});
}

std::array<float, 3> displayRGBFromXYZ(const float X,
                                       const float Y,
                                       const float Z,
                                       const Settings& settings) {
    const auto rgb = ColourCore::projectToRGB(
        ColourCore::XYZ{X, Y, Z},
        ColourCore::OutputSettings{
            .colourSpace = settings.colourSpace,
            .applyGamutMapping = settings.applyGamutMapping
        });
    return {rgb.r, rgb.g, rgb.b};
}

ColourCore::FrameResult buildColourResult(const Frame& frame,
                                          const Settings& settings,
                                          const float loudnessDb) {
    return prepareFrame(frame, settings, loudnessDb).colourResult;
}

}

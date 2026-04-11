#include "audio/analysis/presentation/spectral_presentation.h"

#include <algorithm>
#include <cmath>

#include "audio/analysis/eq/shared_eq_model.h"
#include "audio/analysis/fft/fft_processor.h"
#include "audio/analysis/presentation/phase_colour.h"

namespace SpectralPresentation {

namespace {

void sanitiseMagnitudes(std::vector<float>& magnitudes) {
    for (float& magnitude : magnitudes) {
        if (!std::isfinite(magnitude) || magnitude < 0.0f) {
            magnitude = 0.0f;
        }
    }
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

std::vector<float> buildVisualiserMagnitudes(const Frame& frame,
                                             const Settings& settings) {
    std::vector<float> magnitudes = frame.magnitudes;
    if (magnitudes.empty() || frame.sampleRate <= 0.0f) {
        return magnitudes;
    }

    FFTProcessor::prepareMagnitudesForDisplay(
        magnitudes,
        frame.sampleRate,
        settings.lowGain,
        settings.midGain,
        settings.highGain);

    sanitiseMagnitudes(magnitudes);
    return magnitudes;
}

std::vector<float> buildColourMagnitudes(const Frame& frame,
                                         const Settings& settings) {
    std::vector<float> magnitudes = frame.magnitudes;
    sanitiseMagnitudes(magnitudes);
    AudioEQ::applyMagnitudeResponse(
        magnitudes,
        frame.sampleRate,
        FFTProcessor::FFT_SIZE,
        settings.lowGain,
        settings.midGain,
        settings.highGain);
    return magnitudes;
}

PreparedFrame prepareFrame(const Frame& frame,
                           const Settings& settings,
                           const float loudnessDb,
                           const PhaseAnalysis::PhaseFeatureMetrics& phaseMetrics) {
    PreparedFrame prepared{};
    prepared.visualiserMagnitudes = buildVisualiserMagnitudes(frame, settings);
    const std::vector<float> colourMagnitudes = buildColourMagnitudes(frame, settings);
    prepared.colourResult = ColourMapper::spectrumToColour(
        colourMagnitudes,
        frame.phases,
        frame.frequencies,
        frame.sampleRate,
        settings.colourSpace,
        settings.applyGamutMapping,
        loudnessDb);
    applyPhaseColourInfluence(prepared.colourResult, phaseMetrics, settings);
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
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    ColourMapper::XYZtoRGB(X, Y, Z, r, g, b, settings.colourSpace, true, settings.applyGamutMapping);

    return {r, g, b};
}

ColourMapper::ColourResult buildColourResult(const Frame& frame,
                                             const Settings& settings,
                                             const float loudnessDb) {
    return prepareFrame(frame, settings, loudnessDb).colourResult;
}

}

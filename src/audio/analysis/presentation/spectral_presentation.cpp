#include "audio/analysis/presentation/spectral_presentation.h"

#include <algorithm>
#include <cmath>

#include "audio/analysis/fft/fft_processor.h"

namespace SpectralPresentation {

namespace {

constexpr float kLowCrossover = 200.0f;
constexpr float kHighCrossover = 1900.0f;
constexpr float kLowTransition = 50.0f;
constexpr float kHighTransition = 100.0f;

void sanitiseMagnitudes(std::vector<float>& magnitudes) {
    for (float& magnitude : magnitudes) {
        if (!std::isfinite(magnitude) || magnitude < 0.0f) {
            magnitude = 0.0f;
        }
    }
}

void calculateBandResponses(const float frequency,
                            float& lowResponse,
                            float& midResponse,
                            float& highResponse) {
    lowResponse = std::clamp(
        1.0f - std::max(0.0f, (frequency - kLowCrossover) / kLowTransition),
        0.0f,
        1.0f);
    highResponse = std::clamp((frequency - kHighCrossover) / kHighTransition, 0.0f, 1.0f);
    midResponse = std::clamp(1.0f - lowResponse - highResponse, 0.0f, 1.0f);
}

void applyColourBandGains(std::vector<float>& magnitudes,
                          const float sampleRate,
                          const Settings& settings) {
    if (magnitudes.empty() || sampleRate <= 0.0f) {
        return;
    }

    const bool gainsAreNeutral =
        std::abs(settings.lowGain - 1.0f) < 1e-6f &&
        std::abs(settings.midGain - 1.0f) < 1e-6f &&
        std::abs(settings.highGain - 1.0f) < 1e-6f;
    if (gainsAreNeutral) {
        return;
    }

    for (size_t index = 0; index < magnitudes.size(); ++index) {
        const float frequency = static_cast<float>(index) * sampleRate / static_cast<float>(FFTProcessor::FFT_SIZE);
        float lowResponse = 0.0f;
        float midResponse = 0.0f;
        float highResponse = 0.0f;
        calculateBandResponses(frequency, lowResponse, midResponse, highResponse);
        const float gain =
            lowResponse * settings.lowGain +
            midResponse * settings.midGain +
            highResponse * settings.highGain;
        magnitudes[index] *= std::clamp(gain, 0.0f, 4.0f);
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

PreparedFrame prepareFrame(const Frame& frame,
                           const Settings& settings,
                           const float loudnessDb) {
    PreparedFrame prepared{};
    prepared.visualiserMagnitudes = buildVisualiserMagnitudes(frame, settings);
    std::vector<float> rawMagnitudes = frame.magnitudes;
    sanitiseMagnitudes(rawMagnitudes);
    applyColourBandGains(rawMagnitudes, frame.sampleRate, settings);
    prepared.colourResult = ColourMapper::spectrumToColour(
        rawMagnitudes,
        frame.phases,
        frame.frequencies,
        frame.sampleRate,
        settings.gamma,
        settings.colourSpace,
        settings.applyGamutMapping,
        loudnessDb);
    return prepared;
}

std::array<float, 3> displayRGBFromXYZ(const float X,
                                       const float Y,
                                       const float Z,
                                       const Settings& settings) {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    ColourMapper::XYZtoRGB(X, Y, Z, r, g, b, settings.colourSpace, true, settings.applyGamutMapping);

    const float clampedGamma = std::clamp(settings.gamma, 0.1f, 5.0f);
    if (settings.applyGamutMapping) {
        r = std::pow(std::clamp(r, 0.0f, 1.0f), clampedGamma);
        g = std::pow(std::clamp(g, 0.0f, 1.0f), clampedGamma);
        b = std::pow(std::clamp(b, 0.0f, 1.0f), clampedGamma);
    } else {
        auto applyCurve = [clampedGamma](const float value) {
            if (value <= 0.0f) {
                return value;
            }
            return std::pow(value, clampedGamma);
        };
        r = applyCurve(r);
        g = applyCurve(g);
        b = applyCurve(b);
    }

    return {r, g, b};
}

ColourMapper::ColourResult buildColourResult(const Frame& frame,
                                             const Settings& settings,
                                             const float loudnessDb) {
    return prepareFrame(frame, settings, loudnessDb).colourResult;
}

}

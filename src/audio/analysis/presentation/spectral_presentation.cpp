#include "audio/analysis/presentation/spectral_presentation.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

#include "audio/analysis/eq/shared_eq_model.h"
#include "audio/analysis/fft/fft_processor.h"
#include "audio/analysis/phase/phase_features.h"

namespace SpectralPresentation {

namespace {

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

struct EmissionLines { std::vector<float> magnitudes, frequencies; };

// https://www.dsprelated.com/freebooks/sasp/FFT_Filter_Banks.html
// https://www.w3.org/TR/css-color-4/#interpolation-space
EmissionLines combineBass(const Frame& frame, const Settings& settings, const std::vector<float>& shortMagnitudes) {
    EmissionLines result;
    if (!frame.bass || !std::isfinite(frame.bass->sampleRate) || frame.bass->sampleRate <= 0 ||
        !std::isfinite(frame.bass->coverage) || frame.bass->coverage <= 0 ||
        frame.bass->magnitudes.size() > 513 || !frame.measurementMagnitudes.empty()) return result;
    const auto bassWeight = [](double frequency) {
        if (frequency < 20 || frequency >= 300) return 0.0;
        const double position = std::clamp(std::log2(frequency/150.0),0.0,1.0);
        return 1-position*position*(3-2*position);
    };
    const auto frequencyAt = [&](std::size_t bin) {
        return frame.frequencies.size() == shortMagnitudes.size() ? frame.frequencies[bin] :
            static_cast<float>(bin)*frame.sampleRate/static_cast<float>(resolveFftSize(shortMagnitudes.size()));
    };
    double shortMass = 0, longMass = 0;
    for (std::size_t i = 0; i < shortMagnitudes.size(); ++i) shortMass += shortMagnitudes[i]*bassWeight(frequencyAt(i));
    auto bassMagnitudes = frame.bass->magnitudes;
    const auto cascade = AudioEQ::makeCascade(frame.sampleRate,settings.lowGain,settings.midGain,settings.highGain);
    const bool neutral = settings.lowGain == 1 && settings.midGain == 1 && settings.highGain == 1;
    for (std::size_t i = 0; i < bassMagnitudes.size(); ++i) {
        const float frequency = static_cast<float>(i)*frame.bass->sampleRate/1024;
        float& magnitude = bassMagnitudes[i];
        magnitude = std::isfinite(magnitude) ? std::max(0.f,magnitude) : 0.f;
        if (!neutral) magnitude *= AudioEQ::cascadeMagnitudeResponse(cascade,frequency,frame.sampleRate);
        magnitude = std::isfinite(magnitude) ? std::max(0.f,magnitude) : 0.f;
        longMass += magnitude*bassWeight(frequency);
    }
    if (longMass <= 1e-12 || shortMass <= 0) return result;
    // Redistribute the short window's bass strength using the longer window's shape; do not add unequal grids.
    const double coverage = std::clamp(static_cast<double>(frame.bass->coverage),0.0,1.0);
    const double blend = coverage*coverage*(3-2*coverage);
    result.magnitudes.reserve(shortMagnitudes.size()+bassMagnitudes.size());
    result.frequencies.reserve(shortMagnitudes.size()+bassMagnitudes.size());
    for (std::size_t i = 0; i < shortMagnitudes.size(); ++i) {
        const float frequency = frequencyAt(i);
        result.magnitudes.push_back(static_cast<float>(shortMagnitudes[i]*(1-blend*bassWeight(frequency))));
        result.frequencies.push_back(frequency);
    }
    for (std::size_t i = 0; i < bassMagnitudes.size(); ++i) {
        const float frequency = static_cast<float>(i)*frame.bass->sampleRate/1024;
        if (bassWeight(frequency) <= 0) continue;
        result.magnitudes.push_back(static_cast<float>(bassMagnitudes[i]*bassWeight(frequency)*blend*shortMass/longMass));
        result.frequencies.push_back(frequency);
    }
    return result;
}

}

// https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.circmean.html
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
    const size_t channelLimit = std::min(magnitudes.size(), static_cast<size_t>(std::max<std::uint32_t>(1, channels)));
    if (channelLimit == 1) {
        frame.magnitudes = magnitudes.front();
        sanitiseMagnitudes(frame.magnitudes);
        frame.phases.assign(numBins,0);
        if (!phases.empty() && phases.front().size() == numBins) {
            for (std::size_t i = 0; i < numBins; ++i) {
                const float phase = phases.front()[i];
                if (frame.magnitudes[i] > 0 && std::isfinite(phase))
                    frame.phases[i] = std::abs(phase) <= std::numbers::pi_v<float> ? phase : std::atan2(std::sin(phase),std::cos(phase));
            }
        }
        if (!frequencies.empty() && frequencies.front().size() == numBins) frame.frequencies = frequencies.front();
        return frame;
    }
    frame.magnitudes.assign(numBins, 0.0f);
    frame.phases.assign(numBins, 0.0f);

    size_t contributingMagnitudeChannels = 0;

    std::vector<float> phaseCosines(numBins, 0.0f);
    std::vector<float> phaseSines(numBins, 0.0f);
    std::vector<float> phaseWeights(numBins, 0.0f);
    std::vector<float> strongestWeights(numBins, 0.0f);
    std::vector<float> strongestPhases(numBins, 0.0f);

    for (size_t channelIndex = 0; channelIndex < channelLimit; ++channelIndex) {
        const auto& channelMagnitudes = magnitudes[channelIndex];
        if (channelMagnitudes.size() == numBins) {
            for (size_t bin = 0; bin < numBins; ++bin) {
                const float magnitude = channelMagnitudes[bin];
                if (std::isfinite(magnitude) && magnitude > 0.0f) {
                    frame.magnitudes[bin] += magnitude * magnitude;
                }
            }
            ++contributingMagnitudeChannels;
        }

        if (channelIndex < phases.size()) {
            const auto& channelPhases = phases[channelIndex];
            if (channelPhases.size() == numBins && channelMagnitudes.size() == numBins) {
                for (size_t bin = 0; bin < numBins; ++bin) {
                    const float weight = channelMagnitudes[bin];
                    const float phase = channelPhases[bin];
                    if (std::isfinite(weight) && weight > 0.0f && std::isfinite(phase)) {
                        phaseCosines[bin] += weight * std::cos(phase);
                        phaseSines[bin] += weight * std::sin(phase);
                        phaseWeights[bin] += weight;
                        if (weight > strongestWeights[bin]) {
                            strongestWeights[bin] = weight;
                            strongestPhases[bin] = phase;
                        }
                    }
                }
            }
        }
    }

    if (contributingMagnitudeChannels > 0) {
        const float invChannelCount = 1.0f / static_cast<float>(contributingMagnitudeChannels);
        for (float& magnitude : frame.magnitudes) {
            magnitude = std::sqrt(magnitude * invChannelCount);
        }
    }

    for (size_t bin = 0; bin < numBins; ++bin) {

        frame.phases[bin] = std::hypot(phaseSines[bin], phaseCosines[bin]) > 1e-6f * phaseWeights[bin]
            ? std::atan2(phaseSines[bin], phaseCosines[bin]) : strongestPhases[bin];
    }

    if (!frequencies.empty() && !frequencies.front().empty() && frequencies.front().size() == numBins) {
        frame.frequencies = frequencies.front();
    }

    bool differentMappings = false;
    const auto frequencyAt = [&](size_t channel, size_t bin) {
        return channel < frequencies.size() && frequencies[channel].size() == numBins
            ? frequencies[channel][bin] : static_cast<float>(bin) * sampleRate / resolveFftSize(numBins);
    };
    for (size_t ch = 1; ch < channelLimit; ++ch) {
        for (size_t bin = 0; bin < numBins; ++bin) {
            if (frequencyAt(ch, bin) != frequencyAt(0, bin)) differentMappings = true;
        }
    }
    if (differentMappings && contributingMagnitudeChannels > 0) {
        struct Line { double energy = 0.0; double power = 0.0; };
        std::map<float, Line> lines;
        for (size_t ch = 0; ch < channelLimit; ++ch) {
            if (magnitudes[ch].size() != numBins) continue;
            for (size_t bin = 0; bin < numBins; ++bin) {
                const float frequency = frequencyAt(ch, bin);
                const float magnitude = magnitudes[ch][bin];
                if (!std::isfinite(frequency) || frequency < 0.0f || frequency > sampleRate * 0.5f ||
                    !std::isfinite(magnitude) || magnitude < 0.0f) continue;
                auto& line = lines[frequency];
                const double energy = static_cast<double>(magnitude) * magnitude / contributingMagnitudeChannels;
                line.energy += energy;
                line.power += energy * (bin == 0 || bin + 1 == numBins ? 1.0 : 0.5);
            }
        }
        for (const auto& [frequency, line] : lines) {
            frame.measurementFrequencies.push_back(frequency);
            frame.measurementMagnitudes.push_back(static_cast<float>(std::sqrt(line.energy)));
            frame.measurementPowerWeights.push_back(line.energy > 0.0 ? static_cast<float>(line.power / line.energy) : 0.5f);
        }
        frame.measurementFftSize = resolveFftSize(numBins);
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

PreparedFrame prepareFrame(const Frame& frame,
                           const Settings& settings,
                           const float loudnessDb,
                           const PhaseAnalysis::PhaseFeatureMetrics& phaseMetrics) {
    PreparedFrame prepared{};
    std::vector<float> sharedMagnitudes = buildSharedMagnitudes(frame, settings);
    auto measurementMagnitudes = frame.measurementMagnitudes;
    const bool useLines = !measurementMagnitudes.empty();
    const auto& measurementFrequencies = useLines ? frame.measurementFrequencies : frame.frequencies;
    if (useLines || measurementFrequencies.size() == sharedMagnitudes.size()) {
        if (!useLines) measurementMagnitudes = frame.magnitudes;
        const auto cascade = AudioEQ::makeCascade(frame.sampleRate, settings.lowGain, settings.midGain, settings.highGain);
        for (size_t i = 0; i < measurementMagnitudes.size(); ++i) {
            measurementMagnitudes[i] *= AudioEQ::cascadeMagnitudeResponse(cascade, measurementFrequencies[i], frame.sampleRate);
        }
        sanitiseMagnitudes(measurementMagnitudes);
    }
    const auto hybrid = combineBass(frame,settings,measurementMagnitudes.empty() ? sharedMagnitudes : measurementMagnitudes);
    const ColourCore::EmissionSpectrum emissions{hybrid.magnitudes,hybrid.frequencies};
    const ColourCore::SpectrumLayout layout{frame.measurementFftSize, frame.measurementPowerWeights};
    auto contributions = std::make_shared<ColourPresentation::SpectralContributions>();
    prepared.colourResult = ColourCore::analyseSpectrum(
        measurementMagnitudes.empty() ? sharedMagnitudes : measurementMagnitudes,
        useLines ? std::span<const float>{} : std::span<const float>(frame.phases),
        measurementFrequencies,
        frame.sampleRate,
        ColourCore::OutputSettings{
            .colourSpace = settings.colourSpace,
            .applyGamutMapping = settings.applyGamutMapping
        },
        loudnessDb, &phaseMetrics, useLines ? &layout : nullptr, contributions.get(),
        hybrid.magnitudes.empty() ? nullptr : &emissions);
    prepared.contributions = std::move(contributions);
    prepared.frequencyResolution = frame.sampleRate / static_cast<float>(resolveFftSize(sharedMagnitudes.size()));
    prepared.analysisMagnitudes = std::move(sharedMagnitudes);
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

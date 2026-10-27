#include "audio/analysis/presentation/spectral_presentation.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <complex>
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
    if (!std::isfinite(frame.sampleRate) || frame.sampleRate < 8000 ||
        !frame.bass || !std::isfinite(frame.bass->sampleRate) || frame.bass->sampleRate <= 0 ||
        !std::isfinite(frame.bass->coverage) || frame.bass->coverage <= 0 ||
        frame.bass->magnitudes.size() > 513 || !frame.measurementMagnitudes.empty()) return result;
    const auto bassWeight = [](double frequency) {
        if (!std::isfinite(frequency) || frequency < 20 || frequency >= 300) return 0.0;
        const double position = std::clamp(std::log2(frequency/150.0),0.0,1.0);
        return 1-position*position*(3-2*position);
    };
    const auto frequencyAt = [&](std::size_t bin) {
        return frame.frequencies.size() == shortMagnitudes.size() ? frame.frequencies[bin] :
            static_cast<float>(bin)*frame.sampleRate/static_cast<float>(resolveFftSize(shortMagnitudes.size()));
    };
    auto bassMagnitudes = frame.bass->magnitudes;
    const auto cascade = AudioEQ::makeCascade(frame.sampleRate, settings.lowGain, settings.midGain, settings.highGain);
    for (std::size_t i = 0; i < bassMagnitudes.size(); ++i) {
        const float frequency = static_cast<float>(i) * frame.bass->sampleRate / 1024;
        auto& magnitude = bassMagnitudes[i];
        magnitude = std::isfinite(magnitude) ? std::max(0.f, magnitude) : 0.f;
        magnitude *= AudioEQ::cascadeMagnitudeResponse(cascade, frequency, frame.sampleRate);
    }
    const double resolution = static_cast<double>(frame.sampleRate / static_cast<float>(resolveFftSize(shortMagnitudes.size())));
    std::vector<double> allocated(bassMagnitudes.size());
    std::vector<double> bassFrequencies(bassMagnitudes.size()), bassWeights(bassMagnitudes.size());
    for (std::size_t j = 0; j < bassMagnitudes.size(); ++j) {
        bassFrequencies[j] = static_cast<double>(static_cast<float>(j) * frame.bass->sampleRate / 1024);
        bassWeights[j] = bassWeight(bassFrequencies[j]);
    }
    result.magnitudes.reserve(shortMagnitudes.size() + bassMagnitudes.size());
    result.frequencies.reserve(shortMagnitudes.size() + bassMagnitudes.size());
    // Each short-resolution cell supplies current strength; the older long window supplies only conditional shape.
    // A disjoint old bass peak cannot acquire mass from a newly active short cell.
    // https://www.dsprelated.com/freebooks/sasp/FFT_Filter_Banks.html
    for (std::size_t i = 0; i < shortMagnitudes.size(); ++i) {
        const float frequency = frequencyAt(i);
        const double weight = bassWeight(static_cast<double>(frequency)) * std::clamp(static_cast<double>(frame.bass->coverage), 0.0, 1.0);
        if (weight == 0 || shortMagnitudes[i] == 0) {
            result.magnitudes.push_back(shortMagnitudes[i]);
            result.frequencies.push_back(frequency);
            continue;
        }
        double mass = 0;
        for (std::size_t j = 0; j < bassMagnitudes.size(); ++j) {
            const double longFrequency = bassFrequencies[j];
            mass += static_cast<double>(bassMagnitudes[j]) * bassWeights[j] * std::max(0.0, 1 - std::abs(longFrequency - static_cast<double>(frequency)) / resolution);
        }
        const double transferred = mass > 1e-20 ? static_cast<double>(shortMagnitudes[i]) * weight : 0;
        result.magnitudes.push_back(static_cast<float>(static_cast<double>(shortMagnitudes[i]) - transferred));
        result.frequencies.push_back(frequency);
        if (transferred <= 0) continue;
        for (std::size_t j = 0; j < bassMagnitudes.size(); ++j) {
            const double longFrequency = bassFrequencies[j];
            allocated[j] += transferred * static_cast<double>(bassMagnitudes[j]) * bassWeights[j] *
                std::max(0.0, 1 - std::abs(longFrequency - static_cast<double>(frequency)) / resolution) / mass;
        }
    }
    for (std::size_t j = 0; j < allocated.size(); ++j) {
        if (allocated[j] <= 0) continue;
        result.magnitudes.push_back(static_cast<float>(allocated[j]));
        result.frequencies.push_back(static_cast<float>(j) * frame.bass->sampleRate / 1024);
    }
    return result;
}

}

// https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.circmean.html
Frame mixChannels(const std::vector<std::vector<float>>& magnitudes,
                  const std::vector<std::vector<float>>& phases,
                  const std::vector<std::vector<float>>& frequencies,
                  const std::uint32_t channels,
                  const float sampleRate,
                  const std::vector<std::vector<float>>& kWeightedMagnitudes) {
    if (!kWeightedMagnitudes.empty()) {
        auto frame = mixChannels(magnitudes, phases, frequencies, channels, sampleRate);
        if (kWeightedMagnitudes.size() != channels || magnitudes.size() < channels) return frame;
        for (std::size_t ch = 0; ch < channels; ++ch) {
            if (kWeightedMagnitudes[ch].size() != magnitudes[ch].size() || kWeightedMagnitudes[ch].empty() ||
                !std::all_of(kWeightedMagnitudes[ch].begin(), kWeightedMagnitudes[ch].end(),
                    [](float value) { return std::isfinite(value) && value >= 0.0f; })) return frame;
        }
        frame.kWeightedMagnitudes.assign(kWeightedMagnitudes.front().size(), 0);
        if (channels == 1) frame.kWeightedMagnitudes = kWeightedMagnitudes.front();
        else {
            for (const auto& channel : kWeightedMagnitudes)
                for (std::size_t bin = 0; bin < channel.size(); ++bin)
                    frame.kWeightedMagnitudes[bin] += channel[bin] * channel[bin];
            const float inverseChannels = 1.0f / static_cast<float>(channels);
            for (auto& magnitude : frame.kWeightedMagnitudes) magnitude = std::sqrt(magnitude * inverseChannels);
        }
        return frame;
    }
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
        frame.brightnessPowerGain = static_cast<float>(contributingMagnitudeChannels);
        const float invChannelCount = 1.0f / static_cast<float>(contributingMagnitudeChannels);
        for (float& magnitude : frame.magnitudes) {
            magnitude = std::sqrt(magnitude * invChannelCount);
        }
    }

    std::vector<std::vector<double>> localPowers(channelLimit, std::vector<double>(numBins));
    for (std::size_t channel = 0; channel < channelLimit; ++channel) {
        if (magnitudes[channel].size() != numBins) continue;
        std::vector<double> cumulative(numBins + 1);
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const double value = static_cast<double>(magnitudes[channel][bin]);
            cumulative[bin + 1] = cumulative[bin] + (std::isfinite(value) && value > 0 ? value * value : 0);
        }
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const auto first = bin > 8 ? bin - 8 : 0, last = std::min(numBins, bin + 9);
            localPowers[channel][bin] = (cumulative[last] - cumulative[first]) / static_cast<double>(last - first);
        }
    }
    struct CrossSpectrum {
        std::vector<std::complex<double>> cross;
        std::vector<double> firstPower, secondPower;
    };
    std::vector<std::vector<CrossSpectrum>> crossSpectra(channelLimit);
    for (std::size_t a = 0; a < channelLimit; ++a) {
        crossSpectra[a].resize(a);
        if (magnitudes[a].size() != numBins || phases.size() <= a || phases[a].size() != numBins) continue;
        for (std::size_t b = 0; b < a; ++b) {
            if (magnitudes[b].size() != numBins || phases[b].size() != numBins) continue;
            auto& pair = crossSpectra[a][b];
            pair.cross.resize(numBins); pair.firstPower.resize(numBins); pair.secondPower.resize(numBins);
            for (std::size_t j = 0; j < numBins; ++j) {
                const double ma = static_cast<double>(magnitudes[a][j]), mb = static_cast<double>(magnitudes[b][j]);
                if (!std::isfinite(ma) || !std::isfinite(mb) || ma < 0 || mb < 0 ||
                    !std::isfinite(phases[a][j]) || !std::isfinite(phases[b][j])) continue;
                pair.firstPower[j] = ma * ma; pair.secondPower[j] = mb * mb;
                pair.cross[j] = std::polar(ma * mb, static_cast<double>(phases[a][j] - phases[b][j]));
            }
        }
    }
    // Local channel coherence avoids counting duplicated channels as independent spectral observations.
    // https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.coherence.html
    frame.observationShapes.assign(numBins, 1);
    for (std::size_t bin = 0; bin < numBins; ++bin) {
        double total = 0, covariance = 0;
        for (std::size_t a = 0; a < channelLimit; ++a) {
            if (magnitudes[a].size() != numBins) continue;
            const double powerA = localPowers[a][bin];
            total += powerA;
            covariance += powerA * powerA;
            for (std::size_t b = 0; b < a; ++b) {
                if (magnitudes[b].size() != numBins) continue;
                const double powerB = localPowers[b][bin];
                double coherence = 1;
                const auto& pair = crossSpectra[a][b];
                if (!pair.cross.empty()) {
                    std::complex<double> cross{};
                    double aa = 0, bb = 0;
                    const auto first = bin > 8 ? bin - 8 : 0;
                    const auto last = std::min(numBins, bin + 9);
                    for (auto j = first; j < last; ++j) {
                        aa += pair.firstPower[j]; bb += pair.secondPower[j]; cross += pair.cross[j];
                    }
                    const double samples = static_cast<double>(last - first) / (1 + 8.0 / 9 + 1.0 / 18);
                    const double chance = 1 / samples;
                    coherence = aa > 0 && bb > 0 && chance < 1 ?
                        std::clamp((std::norm(cross) / (aa * bb) - chance) / (1 - chance), 0.0, 1.0) : 1;
                }
                covariance += 2 * coherence * powerA * powerB;
            }
        }
        frame.observationShapes[bin] = covariance > 0 ? static_cast<float>(total * total / covariance) : 1;
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
            ? frequencies[channel][bin] : static_cast<float>(bin) * sampleRate / static_cast<float>(resolveFftSize(numBins));
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
                const double energy = static_cast<double>(magnitude) * static_cast<double>(magnitude) / static_cast<double>(contributingMagnitudeChannels);
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
    auto weightedMagnitudes = frame.kWeightedMagnitudes;
    if (!weightedMagnitudes.empty()) {
        AudioEQ::applyMagnitudeResponse(weightedMagnitudes, frame.sampleRate, resolveFftSize(weightedMagnitudes.size()),
            settings.lowGain, settings.midGain, settings.highGain);
    }
    const ColourCore::SpectrumLayout layout{useLines ? frame.measurementFftSize : resolveFftSize(sharedMagnitudes.size()),
        frame.measurementPowerWeights, frame.brightnessPowerGain, weightedMagnitudes};
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
        loudnessDb, &phaseMetrics, &layout, contributions.get(),
        hybrid.magnitudes.empty() ? nullptr : &emissions);
    prepared.contributions = std::move(contributions);
    prepared.frequencyResolution = std::isfinite(frame.sampleRate) && frame.sampleRate > 0.0f ?
        frame.sampleRate / static_cast<float>(resolveFftSize(sharedMagnitudes.size())) : 0.0f;
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

}

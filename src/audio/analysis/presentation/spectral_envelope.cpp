#include "spectral_envelope.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include "audio/analysis/loudness/k_weighting.h"
#include "audio/analysis/eq/shared_eq_model.h"

namespace SpectralPresentation {
namespace {
constexpr double minimumPower = 1e-24;
constexpr double volatilityMemory = 4.0;
constexpr std::array<double, 9> accelerationScales{1e-6, 1e-5, 1e-4, 1e-3, .01, .1, 1, 10, 100};

// Hann overlap: complex coefficient correlation is the window inner product; power correlation is its square.
// https://www.fceia.unr.edu.ar/prodivoz/Harris_1978.pdf
// Continuous Hann integrals avoid a sample-count-dependent approximation at small hops.
double hannCorrelation(double fraction) {
    if (fraction >= 1) return 0;
    const double angle = 2 * std::numbers::pi * fraction;
    return ((1 - fraction) * (1 + .5 * std::cos(angle)) + 3 * std::sin(angle) / (4 * std::numbers::pi)) / 1.5;
}

// At a real FFT endpoint, the adjacent Hann bin's phase noise has kernel w(t) sin(2*pi*t).
// Its variance is 5/32, whereas endpoint real noise has variance 3/8 before one-sided scaling.
double endpointPhaseCorrelation(double fraction) {
    if (fraction >= 1) return 0;
    const auto integral = [fraction](double frequency, double phase) {
        return frequency == 0 ? (1 - fraction) * std::cos(phase) :
            (std::sin(frequency + phase) - std::sin(frequency * fraction + phase)) / frequency;
    };
    constexpr std::array<double, 2> coefficients{.5, -.25};
    double sum = 0;
    for (std::size_t a = 0; a < 2; ++a) for (std::size_t b = 0; b < 2; ++b) {
        const double first = 2 * std::numbers::pi * static_cast<double>(a + 1);
        const double second = 2 * std::numbers::pi * static_cast<double>(b + 1);
        sum += coefficients[a] * coefficients[b] * .5 *
            (integral(first - second, second * fraction) - integral(first + second, -second * fraction));
    }
    return sum / (5.0 / 32);
}

double informationFraction(double dt, double support) {
    const double fraction = std::clamp(dt / support, 1e-5, 1.0);
    double correlation = 1;
    for (int lag = 1; lag * fraction < 1; ++lag) {
        const double value = hannCorrelation(lag * fraction);
        correlation += 2 * value * value;
    }
    return 1 / correlation;
}
}

LogEnvelope::Prediction::Prediction(double elapsed, double duration)
    : dt(elapsed), support(duration), replacement(0) {
    if (!(support > 0) || !std::isfinite(support) || !std::isfinite(dt)) return;
    replacement = -std::expm1(-dt / volatilityMemory);
    for (std::size_t index = 0; index < accelerationScales.size(); ++index) {
        const double q = accelerationScales[index] / (support * support * support);
        levelVariance[index] = q * dt * dt * dt / 3;
        covariance[index] = q * dt * dt / 2;
        rateVariance[index] = q * dt;
    }
}

LogEnvelope::Estimate LogEnvelope::update(double power, double shape, double information, double dt, double support) {
    return update(power, shape, information, Prediction(dt, support));
}

// Log-Gamma likelihood and Gaussian posterior curvature: https://arxiv.org/pdf/1706.01563 (III-B).
// Local-linear continuous-time covariance: https://users.aalto.fi/~ssarkka/pub/cup_book_online_20131111.pdf
// The bounded process-variance grid integrates predictive evidence; it is an application-specific online extension.
LogEnvelope::Estimate LogEnvelope::update(double power, double shape, double information,
    const Prediction& prediction, Moment moment, bool reportResponse) {
    const double dt = prediction.dt, support = prediction.support;
    power = std::isfinite(power) ? std::max(minimumPower, power) : minimumPower;
    if (!(support > 0) || !std::isfinite(support)) return {std::sqrt(power), power, 0};
    shape = std::isfinite(shape) ? std::clamp(shape, .5, 1e6) : 1;
    information = std::isfinite(information) ? information : 1;
    const double observed = std::log(power);
    const double k = shape * std::clamp(information, 1e-5, 1.0);
    if (!ready_) {
        for (auto& state : states_) state = {observed, 0, 1 / shape, 0, 1 / (shape * support * support), 1.0 / static_cast<double>(states_.size())};
        ready_ = true;
        return {std::sqrt(power), power, 0};
    }
    if (!(dt > 0) || !std::isfinite(dt)) return {};
    std::array<double, accelerationScales.size()> scores{}, gains{};
    const double replacement = prediction.replacement;
    State mixture{};
    mixture.p00 = mixture.p01 = mixture.p11 = 0;
    for (const auto& state : states_) {
        mixture.level += state.weight * state.level;
        mixture.rate += state.weight * state.rate;
    }
    for (const auto& state : states_) {
        const double level = state.level - mixture.level, rate = state.rate - mixture.rate;
        mixture.p00 += state.weight * (state.p00 + level * level);
        mixture.p01 += state.weight * (state.p01 + level * rate);
        mixture.p11 += state.weight * (state.p11 + rate * rate);
    }
    double maximum = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < states_.size(); ++index) {
        auto& state = states_[index];
        // Moment-match states conditional on the Markov volatility transition before prediction.
        // https://doi.org/10.1109/9.1299 ; between-component mean differences belong in the covariance.
        const double priorWeight = (1 - replacement) * state.weight + replacement / static_cast<double>(states_.size());
        const double jumped = replacement / static_cast<double>(states_.size()) / priorWeight, stayed = 1 - jumped;
        const double level = stayed * state.level + jumped * mixture.level;
        const double rate = stayed * state.rate + jumped * mixture.rate;
        const double localLevel = state.level - level, localRate = state.rate - rate;
        const double mixedLevel = mixture.level - level, mixedRate = mixture.rate - rate;
        state.p00 = stayed * (state.p00 + localLevel * localLevel) + jumped * (mixture.p00 + mixedLevel * mixedLevel);
        state.p01 = stayed * (state.p01 + localLevel * localRate) + jumped * (mixture.p01 + mixedLevel * mixedRate);
        state.p11 = stayed * (state.p11 + localRate * localRate) + jumped * (mixture.p11 + mixedRate * mixedRate);
        state.level = level;
        state.rate = rate;
        const double predicted = state.level + dt * state.rate;
        const double p00 = std::max(1e-12, state.p00 + 2 * dt * state.p01 + dt * dt * state.p11 + prediction.levelVariance[index]);
        const double p01 = state.p01 + dt * state.p11 + prediction.covariance[index];
        const double p11 = state.p11 + prediction.rateVariance[index];
        double lower = std::min(predicted, observed), upper = std::max(predicted, observed);
        double mode = observed, evaluatedMode = std::numeric_limits<double>::quiet_NaN(), ratio = 0;
        for (int iteration = 0; iteration < 12; ++iteration) {
            ratio = std::exp(std::clamp(observed - mode, -60.0, 60.0));
            evaluatedMode = mode;
            const double gradient = (mode - predicted) / p00 + k * (1 - ratio);
            if (std::abs(gradient) < 1e-9) break;
            if (gradient > 0) upper = mode; else lower = mode;
            const double next = mode - gradient / (1 / p00 + k * ratio);
            const double candidate = next >= lower && next <= upper ? next : (lower + upper) / 2;
            if (std::abs(candidate - mode) < 1e-8) { mode = candidate; break; }
            mode = candidate;
        }
        if (mode != evaluatedMode) ratio = std::exp(std::clamp(observed - mode, -60.0, 60.0));
        const double curvature = k * ratio;
        const double denominator = 1 + p00 * curvature;
        const double shift = mode - predicted;
        const double relative = mode - observed;
        scores[index] = std::log(priorWeight) -
            k * (relative + ratio - 1) - shift * shift / (2 * p00) - .5 * std::log(denominator);
        maximum = std::max(maximum, scores[index]);
        state.level = mode;
        state.rate += p01 / p00 * shift;
        state.p00 = p00 / denominator;
        state.p01 = p01 / denominator;
        state.p11 = std::max(1e-12, p11 - p01 * p01 * curvature / denominator);
        gains[index] = p00 * curvature / denominator;
    }
    double total = 0;
    for (auto& score : scores) { score = std::exp(score - maximum); total += score; }
    Estimate result;
    double gain = 0;
    // Gamma moments preserve expected magnitude separately from expected power.
    // https://dlmf.nist.gov/5.9 ; E[sqrt(Gamma(k,1/k))] = Gamma(k+1/2)/(sqrt(k) Gamma(k)).
    const double magnitudeFactor = moment == Moment::Power ? 0 : (shape > 10000 ? 1 - 1 / (8 * shape) :
        std::exp(std::lgamma(shape + .5) - std::lgamma(shape) - .5 * std::log(shape)));
    for (std::size_t index = 0; index < states_.size(); ++index) {
        auto& state = states_[index];
        state.weight = scores[index] / total;
        if (moment != Moment::Magnitude) result.power += state.weight * std::exp(std::clamp(state.level + .5 * state.p00, -60.0, 30.0));
        if (moment != Moment::Power) result.magnitude += state.weight * magnitudeFactor * std::exp(std::clamp(.5 * state.level + .125 * state.p00, -30.0, 15.0));
        gain += state.weight * gains[index];
    }
    // Equivalent level-update response, for telemetry only; the estimator is not an exponential smoother.
    if (reportResponse) result.response = gain >= 1 ? 0 : -std::log(20.0) * dt / std::log1p(-std::max(1e-12, gain));
    return result;
}

void SpectralEnvelope::filter(std::vector<float>& magnitudes, const std::vector<float>& phases,
    std::vector<Bin>& bins, double dt, double support, bool powerOutput, std::vector<float>* responses, const std::vector<float>& shapes, bool nyquist) {
    if (bins.size() != magnitudes.size()) bins.assign(magnitudes.size(), {});
    const double information = informationFraction(dt, support);
    const LogEnvelope::Prediction prediction(dt, support);
    const double phaseWeight = -std::expm1(-dt / std::max(support, .1));
    const double phaseVarianceMultiplier = std::max(1e-12,
        3 - 4 * hannCorrelation(dt / support) + hannCorrelation(2 * dt / support));
    const double endpointPhaseMultiplier = std::max(1e-12,
        3 - 4 * endpointPhaseCorrelation(dt / support) + endpointPhaseCorrelation(2 * dt / support));
    if (responses) responses->assign(magnitudes.size(), 0);
    std::vector<double> coherentFractions(magnitudes.size());
    for (std::size_t index = 0; index < magnitudes.size(); ++index) {
        auto& bin = bins[index];
        const double magnitude = static_cast<double>(std::isfinite(magnitudes[index]) ? std::max(0.f, magnitudes[index]) : 0);
        double coherent = 0;
        if (phases.size() == magnitudes.size() && std::isfinite(phases[index]) && magnitude > 1e-12) {
            const double phase = static_cast<double>(phases[index]);
            if (bin.hasPhase) {
                const double expected = 2 * std::numbers::pi * static_cast<double>(index) * dt / support;
                const double velocity = (expected + std::remainder(phase - bin.phase - expected, 2 * std::numbers::pi)) / dt;
                if (bin.hasVelocity) {
                    const double consistency = std::cos(std::remainder((velocity - bin.velocity) * dt, 2 * std::numbers::pi));
                    bin.coherence += phaseWeight * (consistency - bin.coherence);
                    // For concentrated Rician phase, var(phase) = (1-c)/(2c).
                    // The second difference has half-variance (3-4*rho(1)+rho(2))*var(phase).
                    // https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.ncx2.html
                    coherent = bin.coherence > 0 ? 1 / (1 - 2 * std::log(std::min(1.0, bin.coherence)) / phaseVarianceMultiplier) : 0;
                }
                bin.velocity = velocity;
                bin.hasVelocity = true;
            }
            bin.phase = phase;
            bin.hasPhase = true;
        } else { bin.coherence = 0; bin.hasPhase = bin.hasVelocity = false; }
        coherentFractions[index] = coherent;
    }
    for (std::size_t index = 0; index < magnitudes.size(); ++index) {
        auto& bin = bins[index];
        const double magnitude = static_cast<double>(std::isfinite(magnitudes[index]) ? std::max(0.f, magnitudes[index]) : 0);
        double coherent = coherentFractions[index];
        // Real endpoint phase is only a sign. Its neighbour supplies phase-noise evidence with a different kernel.
        if (index == 0 || (nyquist && index + 1 == magnitudes.size())) {
            const auto neighbour = index == 0 ? 1 : index - 1;
            const double consistency = bins.size() > 2 ? bins[neighbour].coherence : 0;
            coherent = consistency > 0 ? 1 / (1 - .6 * std::log(std::min(1.0, consistency)) / endpointPhaseMultiplier) : 0;
        }
        // Moment-matched noncentral complex power: var/mean^2 = 1 - coherentFraction^2.
        // This approximation is deliberately finite even for a perfectly coherent sinusoid.
        const double endpoint = index == 0 || (nyquist && index + 1 == magnitudes.size()) ? .5 : 1;
        const double channels = shapes.size() == magnitudes.size() && std::isfinite(shapes[index]) ?
            std::max(1.0, static_cast<double>(shapes[index])) : 1;
        const double shape = channels * endpoint / std::max(1e-5, 1 - coherent * coherent);
        bin.shape = shape;
        if (magnitude == 0) {
            bin.envelope = {};
            magnitudes[index] = 0;
            continue;
        }
        const auto estimate = bin.envelope.update(magnitude * magnitude, shape, information, prediction,
            powerOutput ? LogEnvelope::Moment::Power : LogEnvelope::Moment::Magnitude, responses != nullptr);
        magnitudes[index] = magnitude == 0 ? 0 : static_cast<float>(powerOutput ? std::sqrt(estimate.power) : estimate.magnitude);
        if (responses) (*responses)[index] = static_cast<float>(estimate.response);
    }
}

Frame SpectralEnvelope::process(const Frame& source, double timestamp, const Settings& settings) {
    if (!std::isfinite(timestamp) || (!std::isfinite(source.sampleRate) || source.sampleRate < 8000 || source.sampleRate > 768000) || source.magnitudes.size() < 2) return source;
    const double sampleRate = static_cast<double>(source.sampleRate);
    if (timestamp < timestamp_ || rate_ != sampleRate || bins_.size() != source.magnitudes.size()) *this = {};
    if (timestamp == timestamp_) return previous_;
    if (source.measurementFrequencies != previous_.measurementFrequencies) lineBins_.clear();
    if (source.kWeightedMagnitudes.empty() != previous_.kWeightedMagnitudes.empty()) brightness_ = {};
    const double support = 2.0 * static_cast<double>(source.magnitudes.size() - 1) / sampleRate;
    const double dt = std::isfinite(timestamp_) ? timestamp - timestamp_ : support;
    if (dt > std::max(2.0, 4 * support)) *this = {};
    const std::array<float, 3> gains{settings.lowGain, settings.midGain, settings.highGain};
    if (gains != equaliser_) { brightness_ = {}; equaliser_ = gains; brightnessWeights_.clear(); }
    const auto equaliser = AudioEQ::makeCascade(source.sampleRate, gains[0], gains[1], gains[2]);
    Frame output = source;
    filter(output.magnitudes, source.phases, bins_, dt, support, false, &responses_, source.observationShapes);
    filter(output.kWeightedMagnitudes, source.phases, weightedBins_, dt, support, true, nullptr, source.observationShapes);
    const bool lines = !output.measurementMagnitudes.empty();
    if (lines) filter(output.measurementMagnitudes, {}, lineBins_, dt, support, false, nullptr, {}, false);
    const bool measured = source.kWeightedMagnitudes.size() == source.magnitudes.size();
    const auto& original = measured ? source.kWeightedMagnitudes : (lines ? source.measurementMagnitudes : source.magnitudes);
    const auto& estimated = measured ? output.kWeightedMagnitudes : (lines ? output.measurementMagnitudes : output.magnitudes);
    const auto& observations = measured ? weightedBins_ : (lines ? lineBins_ : bins_);
    if (brightnessWeights_.size() != original.size() || measuredBrightness_ != measured ||
        (!measured && (source.frequencies != previous_.frequencies ||
            source.measurementFrequencies != previous_.measurementFrequencies ||
            source.measurementPowerWeights != previous_.measurementPowerWeights))) {
        const auto kWeighting = AudioAnalysis::kWeighting(sampleRate);
        brightnessWeights_.resize(original.size());
        measuredBrightness_ = measured;
        for (std::size_t i = 0; i < original.size(); ++i) {
            double weight = i == 0 || i + 1 == original.size() ? 1 : .5;
            if (lines && !measured && source.measurementPowerWeights.size() == original.size()) weight = static_cast<double>(source.measurementPowerWeights[i]);
            const double frequency = !measured && lines && source.measurementFrequencies.size() == original.size() ?
                static_cast<double>(source.measurementFrequencies[i]) :
                (!measured && source.frequencies.size() == original.size() ?
                    static_cast<double>(source.frequencies[i]) : static_cast<double>(i) / support);
            const bool validFrequency = std::isfinite(frequency) && frequency >= 0 && frequency <= sampleRate * .5;
            if (!measured) weight *= validFrequency ? AudioAnalysis::kWeightingPower(kWeighting, frequency, sampleRate) : 0;
            const double eq = static_cast<double>(validFrequency ? AudioEQ::cascadeMagnitudeResponse(equaliser, static_cast<float>(frequency), source.sampleRate) : 0);
            weight *= eq * eq;
            weight = std::isfinite(weight) ? std::max(0.0, weight) : 0;
            brightnessWeights_[i] = weight;
        }
    }
    std::vector<double> deviations(original.size());
    double power = 0, variance = 0, estimatedPower = 0;
    for (std::size_t i = 0; i < original.size(); ++i) {
        const double weight = brightnessWeights_[i];
        const double value = static_cast<double>(std::isfinite(original[i]) ? original[i] : 0);
        const double energy = value * value * weight;
        deviations[i] = energy / std::sqrt(observations[i].shape);
        power += energy;
        estimatedPower += static_cast<double>(estimated[i]) * static_cast<double>(estimated[i]) * weight;
        variance += deviations[i] * deviations[i];
        // The squared Hann window couples adjacent complex bins by -2/3 and next-neighbours by 1/6.
        // https://www.dsprelated.com/freebooks/sasp/Spectrum_Analysis_Noise.html
        for (std::size_t lag = 1; lag <= 2 && lag <= i; ++lag)
            variance += 2 * deviations[i] * deviations[i - lag] * (lag == 1 ? 4.0 / 9 : 1.0 / 36);
    }
    const double shape = variance > 0 ? power * power / variance : 1;
    if (power > 0) {
        const auto brightness = brightness_.update(power, shape, informationFraction(dt, support), dt, support);
        if (estimatedPower > 0) output.brightnessPowerGain *= static_cast<float>(brightness.power / estimatedPower);
    } else brightness_ = {};
    if (source.bass && source.bass->sampleRate > 0) {
        if (previous_.bass && (source.bass->sampleRate != previous_.bass->sampleRate ||
            source.bass->coverage < previous_.bass->coverage)) bassBins_.clear();
        auto bass = std::make_shared<AudioAnalysis::BassSpectrum>(*source.bass);
        filter(bass->magnitudes, {}, bassBins_, dt, static_cast<double>(AudioAnalysis::BassSpectrum::windowSize / bass->sampleRate), false, nullptr, {}, false);
        output.bass = std::move(bass);
    } else bassBins_.clear();
    rate_ = sampleRate;
    timestamp_ = timestamp;
    previous_ = output;
    return output;
}
}

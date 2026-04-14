#include "shared_eq_model.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace AudioEQ {

namespace {

constexpr float kMinimumLinearGain = 0.001f;

float calculateAWeighting(const float frequency) {
    if (frequency <= 0.0f) {
        return 0.0f;
    }

    const float f2 = frequency * frequency;
    const float numerator = 12194.0f * 12194.0f * f2 * f2;
    const float denominator = (f2 + 20.6f * 20.6f) *
        std::sqrt((f2 + 107.7f * 107.7f) * (f2 + 737.9f * 737.9f)) *
        (f2 + 12194.0f * 12194.0f);

    const float aWeight = numerator / denominator;
    const float dbAdjustment = 20.0f * std::log10(aWeight) + 2.0f;
    return std::exp(dbAdjustment * 0.11512925f);
}

float gainDbFromLinear(const float linearGain) {
    return 20.0f * std::log10(std::max(linearGain, kMinimumLinearGain));
}

BiquadCoefficients makeLowShelf(const float sampleRate, const float frequency, const float linearGain) {
    const float gainDb = gainDbFromLinear(linearGain);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * 3.14159265358979323846f * frequency / sampleRate;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / 2.0f * std::sqrt(2.0f);
    const float beta = 2.0f * std::sqrt(A) * alpha;

    const float b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + beta);
    const float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
    const float b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - beta);
    const float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + beta;
    const float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
    const float a2 = (A + 1.0f) + (A - 1.0f) * cosw0 - beta;

    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

BiquadCoefficients makeHighShelf(const float sampleRate, const float frequency, const float linearGain) {
    const float gainDb = gainDbFromLinear(linearGain);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * 3.14159265358979323846f * frequency / sampleRate;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / 2.0f * std::sqrt(2.0f);
    const float beta = 2.0f * std::sqrt(A) * alpha;

    const float b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + beta);
    const float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
    const float b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - beta);
    const float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + beta;
    const float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
    const float a2 = (A + 1.0f) - (A - 1.0f) * cosw0 - beta;

    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

BiquadCoefficients makePeaking(const float sampleRate, const float frequency, const float q, const float linearGain) {
    const float gainDb = gainDbFromLinear(linearGain);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * 3.14159265358979323846f * frequency / sampleRate;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * q);

    const float b0 = 1.0f + alpha * A;
    const float b1 = -2.0f * cosw0;
    const float b2 = 1.0f - alpha * A;
    const float a0 = 1.0f + alpha / A;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha / A;

    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

float magnitudeResponse(const BiquadCoefficients& coefficients, const float frequency, const float sampleRate) {
    if (sampleRate <= 0.0f) {
        return 1.0f;
    }

    const float w = 2.0f * 3.14159265358979323846f * frequency / sampleRate;
    const std::complex<float> z1 = std::exp(std::complex<float>(0.0f, -w));
    const std::complex<float> z2 = std::exp(std::complex<float>(0.0f, -2.0f * w));
    const std::complex<float> numerator =
        coefficients.b0 +
        coefficients.b1 * z1 +
        coefficients.b2 * z2;
    const std::complex<float> denominator =
        1.0f +
        coefficients.a1 * z1 +
        coefficients.a2 * z2;

    const float denominatorMagnitude = std::abs(denominator);
    if (denominatorMagnitude <= 1e-9f) {
        return 1.0f;
    }

    return std::abs(numerator) / denominatorMagnitude;
}

}

CascadeCoefficients makeCascade(const float sampleRate,
                                const float lowGain,
                                const float midGain,
                                const float highGain) {
    CascadeCoefficients cascade{};
    cascade.low = makeLowShelf(sampleRate, kLowCrossoverHz, lowGain);
    cascade.mid = makePeaking(sampleRate, kMidCentreHz, kMidQ, midGain);
    cascade.high = makeHighShelf(sampleRate, kHighCrossoverHz, highGain);
    return cascade;
}

float processSample(const BiquadCoefficients& coefficients,
                    const float input,
                    float& z1,
                    float& z2) {
    const float output = coefficients.b0 * input + z1;
    z1 = coefficients.b1 * input - coefficients.a1 * output + z2;
    z2 = coefficients.b2 * input - coefficients.a2 * output;
    return output;
}

float cascadeMagnitudeResponse(const CascadeCoefficients& cascade,
                               const float frequency,
                               const float sampleRate) {
    return magnitudeResponse(cascade.low, frequency, sampleRate) *
        magnitudeResponse(cascade.mid, frequency, sampleRate) *
        magnitudeResponse(cascade.high, frequency, sampleRate);
}

float perceptualWeightingGain(const float frequency,
                              const PerceptualWeighting weighting) {
    switch (weighting) {
        case PerceptualWeighting::AWeighted:
            return calculateAWeighting(frequency);
        case PerceptualWeighting::None:
        default:
            return 1.0f;
    }
}

void applyMagnitudeResponse(std::vector<float>& magnitudes,
                            const float sampleRate,
                            const size_t fftSize,
                            const float lowGain,
                            const float midGain,
                            const float highGain,
                            const PerceptualWeighting weighting) {
    if (magnitudes.empty() || sampleRate <= 0.0f || fftSize == 0) {
        return;
    }

    const bool neutral =
        std::abs(lowGain - 1.0f) < 1e-6f &&
        std::abs(midGain - 1.0f) < 1e-6f &&
        std::abs(highGain - 1.0f) < 1e-6f;
    if (neutral && weighting == PerceptualWeighting::None) {
        return;
    }

    struct CachedResponse {
        float sampleRate = 0.0f;
        size_t fftSize = 0;
        float lowGain = 1.0f;
        float midGain = 1.0f;
        float highGain = 1.0f;
        PerceptualWeighting weighting = PerceptualWeighting::None;
        std::vector<float> response;
    };

    thread_local CachedResponse cache;
    const bool cacheValid =
        cache.sampleRate == sampleRate &&
        cache.fftSize == fftSize &&
        cache.lowGain == lowGain &&
        cache.midGain == midGain &&
        cache.highGain == highGain &&
        cache.weighting == weighting &&
        cache.response.size() == magnitudes.size();

    if (!cacheValid) {
        cache.sampleRate = sampleRate;
        cache.fftSize = fftSize;
        cache.lowGain = lowGain;
        cache.midGain = midGain;
        cache.highGain = highGain;
        cache.weighting = weighting;
        cache.response.resize(magnitudes.size());

        const auto cascade = makeCascade(sampleRate, lowGain, midGain, highGain);
        for (size_t index = 0; index < cache.response.size(); ++index) {
            const float frequency = static_cast<float>(index) * sampleRate / static_cast<float>(fftSize);
            float response = cascadeMagnitudeResponse(cascade, frequency, sampleRate);
            response *= perceptualWeightingGain(frequency, weighting);
            cache.response[index] = std::clamp(response, 0.0f, 4.0f);
        }
    }

    for (size_t index = 0; index < magnitudes.size(); ++index) {
        magnitudes[index] *= cache.response[index];
    }
}

}

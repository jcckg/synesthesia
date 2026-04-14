#pragma once

#include <cstddef>
#include <vector>

namespace AudioEQ {

enum class PerceptualWeighting {
    None,
    AWeighted
};

struct BiquadCoefficients {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

struct CascadeCoefficients {
    BiquadCoefficients low;
    BiquadCoefficients mid;
    BiquadCoefficients high;
};

constexpr float kLowCrossoverHz = 200.0f;
constexpr float kHighCrossoverHz = 1900.0f;
constexpr float kMidCentreHz = 700.0f;
constexpr float kMidQ = 0.65f;

CascadeCoefficients makeCascade(float sampleRate,
                                float lowGain,
                                float midGain,
                                float highGain);

float processSample(const BiquadCoefficients& coefficients,
                    float input,
                    float& z1,
                    float& z2);

float cascadeMagnitudeResponse(const CascadeCoefficients& cascade,
                               float frequency,
                               float sampleRate);

float perceptualWeightingGain(float frequency,
                              PerceptualWeighting weighting = PerceptualWeighting::None);

void applyMagnitudeResponse(std::vector<float>& magnitudes,
                            float sampleRate,
                            size_t fftSize,
                            float lowGain,
                            float midGain,
                            float highGain,
                            PerceptualWeighting weighting = PerceptualWeighting::None);

}

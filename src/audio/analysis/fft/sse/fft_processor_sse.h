#pragma once

#if defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <immintrin.h>
#include <span>
#include <vector>
#include "kiss_fftr.h"

namespace FFTProcessorSSE {
    void applyHannWindow(std::span<float> output, std::span<const float> input,
                        std::span<const float> window);

    void calculateMagnitudes(std::span<float> magnitudes, std::span<const float> real,
                           std::span<const float> imag);

    void calculateMagnitudesFromComplex(std::span<float> magnitudes,
                                       const kiss_fft_cpx* fft_output, size_t count);

    void calculateSpectralEnergy(std::span<float> envelope, std::span<const float> real,
                                std::span<const float> imag, float totalEnergyInv);

    void applyEQGains(std::span<float> magnitudes, std::span<const float> frequencies,
                     float lowGain, float midGain, float highGain,
                     float sampleRate, size_t minBin, size_t maxBin);

    void vectorMultiply(std::span<float> result, std::span<const float> a,
                       std::span<const float> b);
    void vectorScale(std::span<float> data, float scale);
    void vectorFill(std::span<float> data, float value);
    float vectorSum(std::span<const float> data);
    float vectorMax(std::span<const float> data);

    bool isSSEAvailable();
}

#endif

#pragma once

#if defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <immintrin.h>
#include <span>
#include <vector>
#include "constants.h"

namespace ColourMapperSSE {

using ColourSpace = ::ColourSpace;
    void rgbToLab(std::span<const float> r, std::span<const float> g, std::span<const float> b,
                  std::span<float> L, std::span<float> a, std::span<float> b_comp, size_t count,
                  ColourSpace colourSpace = ColourSpace::Rec2020);

    void labToRgb(std::span<const float> L, std::span<const float> a, std::span<const float> b_comp,
                  std::span<float> r, std::span<float> g, std::span<float> b, size_t count,
                  ColourSpace colourSpace = ColourSpace::Rec2020);

    void rgbToOklab(std::span<const float> r, std::span<const float> g, std::span<const float> b,
                    std::span<float> L, std::span<float> a, std::span<float> b_comp, size_t count,
                    ColourSpace colourSpace = ColourSpace::Rec2020);

    void oklabToRgb(std::span<const float> L, std::span<const float> a, std::span<const float> b_comp,
                    std::span<float> r, std::span<float> g, std::span<float> b, size_t count,
                    ColourSpace colourSpace = ColourSpace::Rec2020);

    void xyzToRgb(std::span<const float> X, std::span<const float> Y, std::span<const float> Z,
                  std::span<float> r, std::span<float> g, std::span<float> b, size_t count,
                  ColourSpace colourSpace = ColourSpace::Rec2020);

    void rgbToXyz(std::span<const float> r, std::span<const float> g, std::span<const float> b,
                  std::span<float> X, std::span<float> Y, std::span<float> Z, size_t count,
                  ColourSpace colourSpace = ColourSpace::Rec2020);

    void vectorLerp(std::span<float> result, std::span<const float> a, std::span<const float> b,
                   std::span<const float> t, size_t count);

    void vectorClamp(std::span<float> data, float min_val, float max_val, size_t count);

    void vectorPow(std::span<float> data, float exponent, size_t count);

    void vectorLog(std::span<float> result, std::span<const float> input, size_t count);

    void vectorExp(std::span<float> result, std::span<const float> input, size_t count);

    void vectorSqrt(std::span<float> result, std::span<const float> input, size_t count);

    void enhanceColourWithAudioParams(float& r, float& g, float& b,
                                      float loudness, float spectralFlatness,
                                      float spectralCentroid, float spectralSpread,
                                      float totalEnergy, float spectralRolloff,
                                      float spectralCrestFactor, float spectralFlux,
                                      bool onsetDetected,
                                      ColourSpace colourSpace = ColourSpace::Rec2020);

    bool isSSEAvailable();
}

#endif

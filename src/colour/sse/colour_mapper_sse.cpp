#include "colour_mapper_sse.h"
#include "colour_mapper.h"

#if defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)

#include <algorithm>
#include <cmath>

namespace ColourMapperSSE {

using ColourSpace = ::ColourSpace;

bool isSSEAvailable() {
    return true;
}

// Accurate SIMD math approximations for SSE
// Polynomial coefficients match NEON implementation for consistency
namespace AccurateMath {
    // Fast SSE log2 approximation using bit manipulation and polynomial approximation
    inline __m128 accurateLog2(__m128 x) {
        x = _mm_max_ps(x, _mm_set1_ps(1e-7f));

        // Extract exponent by bit shifting
        __m128i exp_i32 = _mm_castps_si128(x);
        exp_i32 = _mm_srli_epi32(exp_i32, 23);
        exp_i32 = _mm_sub_epi32(exp_i32, _mm_set1_epi32(127));
        __m128 exp_f32 = _mm_cvtepi32_ps(exp_i32);

        // Extract mantissa
        __m128i mantissa_i32 = _mm_castps_si128(x);
        mantissa_i32 = _mm_and_si128(mantissa_i32, _mm_set1_epi32(0x007FFFFF));
        mantissa_i32 = _mm_or_si128(mantissa_i32, _mm_set1_epi32(0x3F800000));
        __m128 m = _mm_castsi128_ps(mantissa_i32);

        // 4th-degree polynomial approximation for log2(mantissa)
        const __m128 c0 = _mm_set1_ps(-3.4436f);
        const __m128 c1 = _mm_set1_ps(6.9814f);
        const __m128 c2 = _mm_set1_ps(-5.4918f);
        const __m128 c3 = _mm_set1_ps(2.9452f);
        const __m128 c4 = _mm_set1_ps(-0.9892f);

        __m128 m2 = _mm_mul_ps(m, m);
        __m128 m3 = _mm_mul_ps(m2, m);
        __m128 m4 = _mm_mul_ps(m3, m);

        __m128 poly = c0;
        poly = _mm_add_ps(poly, _mm_mul_ps(c1, m));
        poly = _mm_add_ps(poly, _mm_mul_ps(c2, m2));
        poly = _mm_add_ps(poly, _mm_mul_ps(c3, m3));
        poly = _mm_add_ps(poly, _mm_mul_ps(c4, m4));

        return _mm_add_ps(exp_f32, poly);
    }

    // Fast SSE exp2 approximation
    inline __m128 accurateExp2(__m128 x) {
        x = _mm_max_ps(x, _mm_set1_ps(-126.0f));
        x = _mm_min_ps(x, _mm_set1_ps(126.0f));

        __m128i exp_i32 = _mm_cvtps_epi32(x);
        __m128 frac = _mm_sub_ps(x, _mm_cvtepi32_ps(exp_i32));

        // 4th-degree polynomial for 2^frac where frac in [0,1)
        const __m128 c0 = _mm_set1_ps(1.0f);
        const __m128 c1 = _mm_set1_ps(0.6931f);
        const __m128 c2 = _mm_set1_ps(0.2402f);
        const __m128 c3 = _mm_set1_ps(0.0556f);
        const __m128 c4 = _mm_set1_ps(0.0096f);

        __m128 frac2 = _mm_mul_ps(frac, frac);
        __m128 frac3 = _mm_mul_ps(frac2, frac);
        __m128 frac4 = _mm_mul_ps(frac3, frac);

        __m128 poly = c0;
        poly = _mm_add_ps(poly, _mm_mul_ps(c1, frac));
        poly = _mm_add_ps(poly, _mm_mul_ps(c2, frac2));
        poly = _mm_add_ps(poly, _mm_mul_ps(c3, frac3));
        poly = _mm_add_ps(poly, _mm_mul_ps(c4, frac4));

        // Combine with integer exponent: result = poly * 2^exp_i32
        exp_i32 = _mm_add_epi32(exp_i32, _mm_set1_epi32(127));
        exp_i32 = _mm_slli_epi32(exp_i32, 23);
        __m128 scale = _mm_castsi128_ps(exp_i32);

        return _mm_mul_ps(poly, scale);
    }

    // Fast SSE pow approximation with special cases for common exponents
    inline __m128 accuratePow(__m128 x, float exponent) {
        x = _mm_max_ps(x, _mm_set1_ps(1e-7f));

        // Special case: x^2.4 (sRGB gamma encoding)
        if (std::abs(exponent - 2.4f) < 1e-5f) {
            const __m128 c0 = _mm_set1_ps(0.0f);
            const __m128 c1 = _mm_set1_ps(2.0703f);
            const __m128 c2 = _mm_set1_ps(1.0984f);
            const __m128 c3 = _mm_set1_ps(-1.5852f);
            const __m128 c4 = _mm_set1_ps(1.0165f);

            __m128 x2 = _mm_mul_ps(x, x);
            __m128 x3 = _mm_mul_ps(x2, x);
            __m128 x4 = _mm_mul_ps(x3, x);

            __m128 result = c0;
            result = _mm_add_ps(result, _mm_mul_ps(c1, x));
            result = _mm_add_ps(result, _mm_mul_ps(c2, x2));
            result = _mm_add_ps(result, _mm_mul_ps(c3, x3));
            result = _mm_add_ps(result, _mm_mul_ps(c4, x4));

            return result;
        }
        // Special case: x^(1/2.4) (sRGB gamma decoding)
        else if (std::abs(exponent - (1.0f / 2.4f)) < 1e-5f) {
            const __m128 c0 = _mm_set1_ps(0.0f);
            const __m128 c1 = _mm_set1_ps(0.9465f);
            const __m128 c2 = _mm_set1_ps(0.3095f);
            const __m128 c3 = _mm_set1_ps(-0.4347f);
            const __m128 c4 = _mm_set1_ps(0.1787f);

            __m128 x2 = _mm_mul_ps(x, x);
            __m128 x3 = _mm_mul_ps(x2, x);
            __m128 x4 = _mm_mul_ps(x3, x);

            __m128 result = c0;
            result = _mm_add_ps(result, _mm_mul_ps(c1, x));
            result = _mm_add_ps(result, _mm_mul_ps(c2, x2));
            result = _mm_add_ps(result, _mm_mul_ps(c3, x3));
            result = _mm_add_ps(result, _mm_mul_ps(c4, x4));

            return result;
        }
        // Special case: x^(1/3) (cube root for Lab conversion)
        else if (std::abs(exponent - (1.0f / 3.0f)) < 1e-5f) {
            const __m128 c0 = _mm_set1_ps(0.0f);
            const __m128 c1 = _mm_set1_ps(0.9309f);
            const __m128 c2 = _mm_set1_ps(0.4017f);
            const __m128 c3 = _mm_set1_ps(-0.4731f);
            const __m128 c4 = _mm_set1_ps(0.1405f);

            __m128 x2 = _mm_mul_ps(x, x);
            __m128 x3 = _mm_mul_ps(x2, x);
            __m128 x4 = _mm_mul_ps(x3, x);

            __m128 result = c0;
            result = _mm_add_ps(result, _mm_mul_ps(c1, x));
            result = _mm_add_ps(result, _mm_mul_ps(c2, x2));
            result = _mm_add_ps(result, _mm_mul_ps(c3, x3));
            result = _mm_add_ps(result, _mm_mul_ps(c4, x4));

            return result;
        }
        // Special case: x^3 (cube)
        else if (std::abs(exponent - 3.0f) < 1e-5f) {
            return _mm_mul_ps(_mm_mul_ps(x, x), x);
        }
        // General case: use log2/exp2
        else {
            __m128 logX = accurateLog2(x);
            __m128 expResult = _mm_mul_ps(logX, _mm_set1_ps(exponent));
            return accurateExp2(expResult);
        }
    }
} // namespace AccurateMath


void rgbToXyz(std::span<const float> r, std::span<const float> g, std::span<const float> b,
              std::span<float> X, std::span<float> Y, std::span<float> Z, size_t count, ColourSpace colourSpace) {
    const size_t size = std::min({r.size(), g.size(), b.size(), X.size(), Y.size(), Z.size(), count});
    const size_t vectorSize = size & ~3u;

    // Rec2020 has different gamma curve than sRGB/DisplayP3
    if (colourSpace == ColourSpace::Rec2020) {
        // Rec2020 RGB to XYZ matrix
        const __m128 r_to_x = _mm_set1_ps(0.63695806f);
        const __m128 g_to_x = _mm_set1_ps(0.1446169f);
        const __m128 b_to_x = _mm_set1_ps(0.16888098f);

        const __m128 r_to_y = _mm_set1_ps(0.2627002f);
        const __m128 g_to_y = _mm_set1_ps(0.67799807f);
        const __m128 b_to_y = _mm_set1_ps(0.05930172f);

        const __m128 r_to_z = _mm_set1_ps(0.0f);
        const __m128 g_to_z = _mm_set1_ps(0.02807269f);
        const __m128 b_to_z = _mm_set1_ps(1.0609851f);

        // Rec2020 gamma decode parameters
        const float beta = 0.01805397f;
        const float alpha = 1.0992968f;
        const __m128 gammaThreshold = _mm_set1_ps(4.5f * beta);
        const __m128 linearSlopeInv = _mm_set1_ps(1.0f / 4.5f);
        const __m128 alphaMinusOne = _mm_set1_ps(alpha - 1.0f);
        const __m128 invAlpha = _mm_set1_ps(1.0f / alpha);

        const __m128 zero = _mm_setzero_ps();
        const __m128 one = _mm_set1_ps(1.0f);

        size_t i = 0;
        for (; i < vectorSize; i += 4) {
            __m128 rVec = _mm_loadu_ps(&r[i]);
            __m128 gVec = _mm_loadu_ps(&g[i]);
            __m128 bVec = _mm_loadu_ps(&b[i]);

            // Clamp to [0,1]
            rVec = _mm_min_ps(_mm_max_ps(rVec, zero), one);
            gVec = _mm_min_ps(_mm_max_ps(gVec, zero), one);
            bVec = _mm_min_ps(_mm_max_ps(bVec, zero), one);

            // Gamma decode: piecewise function
            const __m128 rMask = _mm_cmple_ps(rVec, gammaThreshold);
            const __m128 gMask = _mm_cmple_ps(gVec, gammaThreshold);
            const __m128 bMask = _mm_cmple_ps(bVec, gammaThreshold);

            const __m128 rLow = _mm_mul_ps(rVec, linearSlopeInv);
            const __m128 gLow = _mm_mul_ps(gVec, linearSlopeInv);
            const __m128 bLow = _mm_mul_ps(bVec, linearSlopeInv);

            const __m128 rNorm = _mm_mul_ps(_mm_add_ps(rVec, alphaMinusOne), invAlpha);
            const __m128 gNorm = _mm_mul_ps(_mm_add_ps(gVec, alphaMinusOne), invAlpha);
            const __m128 bNorm = _mm_mul_ps(_mm_add_ps(bVec, alphaMinusOne), invAlpha);

            const __m128 rHigh = AccurateMath::accuratePow(rNorm, 1.0f / 0.45f);
            const __m128 gHigh = AccurateMath::accuratePow(gNorm, 1.0f / 0.45f);
            const __m128 bHigh = AccurateMath::accuratePow(bNorm, 1.0f / 0.45f);

            // Blend based on threshold
            const __m128 rLinear = _mm_or_ps(_mm_and_ps(rMask, rLow), _mm_andnot_ps(rMask, rHigh));
            const __m128 gLinear = _mm_or_ps(_mm_and_ps(gMask, gLow), _mm_andnot_ps(gMask, gHigh));
            const __m128 bLinear = _mm_or_ps(_mm_and_ps(bMask, bLow), _mm_andnot_ps(bMask, bHigh));

            // Matrix multiplication: XYZ = M * RGB
            __m128 XVec = _mm_mul_ps(rLinear, r_to_x);
            XVec = _mm_add_ps(XVec, _mm_mul_ps(gLinear, g_to_x));
            XVec = _mm_add_ps(XVec, _mm_mul_ps(bLinear, b_to_x));

            __m128 YVec = _mm_mul_ps(rLinear, r_to_y);
            YVec = _mm_add_ps(YVec, _mm_mul_ps(gLinear, g_to_y));
            YVec = _mm_add_ps(YVec, _mm_mul_ps(bLinear, b_to_y));

            __m128 ZVec = _mm_mul_ps(rLinear, r_to_z);
            ZVec = _mm_add_ps(ZVec, _mm_mul_ps(gLinear, g_to_z));
            ZVec = _mm_add_ps(ZVec, _mm_mul_ps(bLinear, b_to_z));

            _mm_storeu_ps(&X[i], XVec);
            _mm_storeu_ps(&Y[i], YVec);
            _mm_storeu_ps(&Z[i], ZVec);
        }

        // Scalar remainder
        for (; i < size; ++i) {
            float Xv, Yv, Zv;
            ColourMapper::RGBtoXYZ(std::clamp(r[i], 0.0f, 1.0f),
                                   std::clamp(g[i], 0.0f, 1.0f),
                                   std::clamp(b[i], 0.0f, 1.0f),
                                   Xv, Yv, Zv, colourSpace);
            X[i] = Xv;
            Y[i] = Yv;
            Z[i] = Zv;
        }
        return;
    }

    // sRGB and DisplayP3 use sRGB gamma curve
    const bool useP3 = (colourSpace == ColourSpace::DisplayP3);

    __m128 r_to_x, g_to_x, b_to_x;
    __m128 r_to_y, g_to_y, b_to_y;
    __m128 r_to_z, g_to_z, b_to_z;

    if (useP3) {
        // DisplayP3 RGB to XYZ matrix
        r_to_x = _mm_set1_ps(0.48657095f);
        g_to_x = _mm_set1_ps(0.2656677f);
        b_to_x = _mm_set1_ps(0.19821729f);

        r_to_y = _mm_set1_ps(0.22897457f);
        g_to_y = _mm_set1_ps(0.69173855f);
        b_to_y = _mm_set1_ps(0.07928691f);

        r_to_z = _mm_set1_ps(0.0f);
        g_to_z = _mm_set1_ps(0.04511338f);
        b_to_z = _mm_set1_ps(1.0439444f);
    } else {
        // sRGB to XYZ matrix (IEC 61966-2-1:1999)
        r_to_x = _mm_set1_ps(0.4124564f);
        g_to_x = _mm_set1_ps(0.3575761f);
        b_to_x = _mm_set1_ps(0.1804375f);

        r_to_y = _mm_set1_ps(0.2126729f);
        g_to_y = _mm_set1_ps(0.7151522f);
        b_to_y = _mm_set1_ps(0.0721750f);

        r_to_z = _mm_set1_ps(0.0193339f);
        g_to_z = _mm_set1_ps(0.1191920f);
        b_to_z = _mm_set1_ps(0.9503041f);
    }

    // sRGB gamma decode constants
    const __m128 threshold = _mm_set1_ps(0.04045f);
    const __m128 div_12_92 = _mm_set1_ps(1.0f / 12.92f);
    const __m128 add_0_055 = _mm_set1_ps(0.055f);
    const __m128 div_1_055 = _mm_set1_ps(1.0f / 1.055f);

    const __m128 zero = _mm_setzero_ps();
    const __m128 one = _mm_set1_ps(1.0f);

    size_t i = 0;
    for (; i < vectorSize; i += 4) {
        __m128 rVec = _mm_loadu_ps(&r[i]);
        __m128 gVec = _mm_loadu_ps(&g[i]);
        __m128 bVec = _mm_loadu_ps(&b[i]);

        // Clamp to [0,1]
        rVec = _mm_min_ps(_mm_max_ps(rVec, zero), one);
        gVec = _mm_min_ps(_mm_max_ps(gVec, zero), one);
        bVec = _mm_min_ps(_mm_max_ps(bVec, zero), one);

        // sRGB gamma decode: piecewise function
        const __m128 rMask = _mm_cmple_ps(rVec, threshold);
        const __m128 gMask = _mm_cmple_ps(gVec, threshold);
        const __m128 bMask = _mm_cmple_ps(bVec, threshold);

        const __m128 rLow = _mm_mul_ps(rVec, div_12_92);
        const __m128 gLow = _mm_mul_ps(gVec, div_12_92);
        const __m128 bLow = _mm_mul_ps(bVec, div_12_92);

        const __m128 rNorm = _mm_mul_ps(_mm_add_ps(rVec, add_0_055), div_1_055);
        const __m128 gNorm = _mm_mul_ps(_mm_add_ps(gVec, add_0_055), div_1_055);
        const __m128 bNorm = _mm_mul_ps(_mm_add_ps(bVec, add_0_055), div_1_055);

        const __m128 rHigh = AccurateMath::accuratePow(rNorm, 2.4f);
        const __m128 gHigh = AccurateMath::accuratePow(gNorm, 2.4f);
        const __m128 bHigh = AccurateMath::accuratePow(bNorm, 2.4f);

        // Blend based on threshold
        const __m128 rLinear = _mm_or_ps(_mm_and_ps(rMask, rLow), _mm_andnot_ps(rMask, rHigh));
        const __m128 gLinear = _mm_or_ps(_mm_and_ps(gMask, gLow), _mm_andnot_ps(gMask, gHigh));
        const __m128 bLinear = _mm_or_ps(_mm_and_ps(bMask, bLow), _mm_andnot_ps(bMask, bHigh));

        // Matrix multiplication: XYZ = M * RGB
        __m128 XVec = _mm_mul_ps(rLinear, r_to_x);
        XVec = _mm_add_ps(XVec, _mm_mul_ps(gLinear, g_to_x));
        XVec = _mm_add_ps(XVec, _mm_mul_ps(bLinear, b_to_x));

        __m128 YVec = _mm_mul_ps(rLinear, r_to_y);
        YVec = _mm_add_ps(YVec, _mm_mul_ps(gLinear, g_to_y));
        YVec = _mm_add_ps(YVec, _mm_mul_ps(bLinear, b_to_y));

        __m128 ZVec = _mm_mul_ps(rLinear, r_to_z);
        ZVec = _mm_add_ps(ZVec, _mm_mul_ps(gLinear, g_to_z));
        ZVec = _mm_add_ps(ZVec, _mm_mul_ps(bLinear, b_to_z));

        _mm_storeu_ps(&X[i], XVec);
        _mm_storeu_ps(&Y[i], YVec);
        _mm_storeu_ps(&Z[i], ZVec);
    }

    // Scalar remainder
    for (; i < size; ++i) {
        float Xv, Yv, Zv;
        ColourMapper::RGBtoXYZ(std::clamp(r[i], 0.0f, 1.0f),
                               std::clamp(g[i], 0.0f, 1.0f),
                               std::clamp(b[i], 0.0f, 1.0f),
                               Xv, Yv, Zv, colourSpace);
        X[i] = Xv;
        Y[i] = Yv;
        Z[i] = Zv;
    }
}

void xyzToRgb(std::span<const float> X, std::span<const float> Y, std::span<const float> Z,
              std::span<float> r, std::span<float> g, std::span<float> b, size_t count,
              ColourSpace colourSpace) {
    const size_t size = std::min({X.size(), Y.size(), Z.size(), r.size(), g.size(), b.size(), count});
    const size_t vectorSize = size & ~3u;

    if (colourSpace == ColourSpace::Rec2020) {
        // Rec2020 XYZ to RGB matrix coefficients
        // Reference: ITU-R BT.2020, CIE 15:2018
        const __m128 x_to_r = _mm_set1_ps(1.7166512f);
        const __m128 y_to_r = _mm_set1_ps(-0.35567078f);
        const __m128 z_to_r = _mm_set1_ps(-0.25336629f);

        const __m128 x_to_g = _mm_set1_ps(-0.66668433f);
        const __m128 y_to_g = _mm_set1_ps(1.6164813f);
        const __m128 z_to_g = _mm_set1_ps(0.01576854f);

        const __m128 x_to_b = _mm_set1_ps(0.01763986f);
        const __m128 y_to_b = _mm_set1_ps(-0.04277061f);
        const __m128 z_to_b = _mm_set1_ps(0.94210315f);

        // Rec2020 gamma encoding constants
        const float beta = 0.01805397f;
        const float alpha = 1.0992968f;
        const __m128 betaVec = _mm_set1_ps(beta);
        const __m128 slope = _mm_set1_ps(4.5f);
        const __m128 alphaVec = _mm_set1_ps(alpha);
        const __m128 alphaMinusOne = _mm_set1_ps(alpha - 1.0f);
        const __m128 zero = _mm_set1_ps(0.0f);
        const __m128 one = _mm_set1_ps(1.0f);

        size_t i = 0;
        for (; i < vectorSize; i += 4) {
            __m128 XVec = _mm_loadu_ps(&X[i]);
            __m128 YVec = _mm_loadu_ps(&Y[i]);
            __m128 ZVec = _mm_loadu_ps(&Z[i]);

            // Matrix multiplication: RGB = M * XYZ
            __m128 r_linear = _mm_mul_ps(XVec, x_to_r);
            r_linear = _mm_add_ps(r_linear, _mm_mul_ps(YVec, y_to_r));
            r_linear = _mm_add_ps(r_linear, _mm_mul_ps(ZVec, z_to_r));

            __m128 g_linear = _mm_mul_ps(XVec, x_to_g);
            g_linear = _mm_add_ps(g_linear, _mm_mul_ps(YVec, y_to_g));
            g_linear = _mm_add_ps(g_linear, _mm_mul_ps(ZVec, z_to_g));

            __m128 b_linear = _mm_mul_ps(XVec, x_to_b);
            b_linear = _mm_add_ps(b_linear, _mm_mul_ps(YVec, y_to_b));
            b_linear = _mm_add_ps(b_linear, _mm_mul_ps(ZVec, z_to_b));

            // Rec2020 gamma encode: piecewise function
            // Linear region: abs(linear) <= beta → out = linear * 4.5
            // Power region: abs(linear) > beta → out = sign(linear) * (alpha * abs(linear)^0.45 - (alpha-1))

            // Calculate absolute values for threshold comparison
            __m128 r_abs = _mm_andnot_ps(_mm_set1_ps(-0.0f), r_linear);  // abs by clearing sign bit
            __m128 g_abs = _mm_andnot_ps(_mm_set1_ps(-0.0f), g_linear);
            __m128 b_abs = _mm_andnot_ps(_mm_set1_ps(-0.0f), b_linear);

            // Threshold masks: true if abs(linear) <= beta (use linear region)
            __m128 r_mask = _mm_cmple_ps(r_abs, betaVec);
            __m128 g_mask = _mm_cmple_ps(g_abs, betaVec);
            __m128 b_mask = _mm_cmple_ps(b_abs, betaVec);

            // Linear region: linear * slope
            __m128 r_low = _mm_mul_ps(r_linear, slope);
            __m128 g_low = _mm_mul_ps(g_linear, slope);
            __m128 b_low = _mm_mul_ps(b_linear, slope);

            // Power region magnitude: abs(linear)^0.45
            __m128 r_high_mag = AccurateMath::accuratePow(r_abs, 0.45f);
            __m128 g_high_mag = AccurateMath::accuratePow(g_abs, 0.45f);
            __m128 b_high_mag = AccurateMath::accuratePow(b_abs, 0.45f);

            // Apply: alpha * mag - (alpha - 1)
            r_high_mag = _mm_sub_ps(_mm_mul_ps(alphaVec, r_high_mag), alphaMinusOne);
            g_high_mag = _mm_sub_ps(_mm_mul_ps(alphaVec, g_high_mag), alphaMinusOne);
            b_high_mag = _mm_sub_ps(_mm_mul_ps(alphaVec, b_high_mag), alphaMinusOne);

            // Restore sign: check if original linear was negative
            __m128 r_sign_mask = _mm_cmplt_ps(r_linear, zero);
            __m128 g_sign_mask = _mm_cmplt_ps(g_linear, zero);
            __m128 b_sign_mask = _mm_cmplt_ps(b_linear, zero);

            // Negate if original was negative
            __m128 r_high = _mm_or_ps(_mm_and_ps(r_sign_mask, _mm_sub_ps(zero, r_high_mag)),
                                      _mm_andnot_ps(r_sign_mask, r_high_mag));
            __m128 g_high = _mm_or_ps(_mm_and_ps(g_sign_mask, _mm_sub_ps(zero, g_high_mag)),
                                      _mm_andnot_ps(g_sign_mask, g_high_mag));
            __m128 b_high = _mm_or_ps(_mm_and_ps(b_sign_mask, _mm_sub_ps(zero, b_high_mag)),
                                      _mm_andnot_ps(b_sign_mask, b_high_mag));

            // Select between linear and power regions
            __m128 r_out = _mm_or_ps(_mm_and_ps(r_mask, r_low), _mm_andnot_ps(r_mask, r_high));
            __m128 g_out = _mm_or_ps(_mm_and_ps(g_mask, g_low), _mm_andnot_ps(g_mask, g_high));
            __m128 b_out = _mm_or_ps(_mm_and_ps(b_mask, b_low), _mm_andnot_ps(b_mask, b_high));

            // Clamp to [0, 1]
            r_out = _mm_max_ps(_mm_min_ps(r_out, one), zero);
            g_out = _mm_max_ps(_mm_min_ps(g_out, one), zero);
            b_out = _mm_max_ps(_mm_min_ps(b_out, one), zero);

            _mm_storeu_ps(&r[i], r_out);
            _mm_storeu_ps(&g[i], g_out);
            _mm_storeu_ps(&b[i], b_out);
        }

        // Scalar remainder loop
        for (; i < size; ++i) {
            float rOut, gOut, bOut;
            ColourMapper::XYZtoRGB(X[i], Y[i], Z[i], rOut, gOut, bOut, colourSpace, true, true);
            r[i] = std::clamp(rOut, 0.0f, 1.0f);
            g[i] = std::clamp(gOut, 0.0f, 1.0f);
            b[i] = std::clamp(bOut, 0.0f, 1.0f);
        }
        return;
    }

    // sRGB and DisplayP3 share the same gamma encoding (sRGB curve)
    const bool useP3 = (colourSpace == ColourSpace::DisplayP3);

    __m128 x_to_r, y_to_r, z_to_r;
    __m128 x_to_g, y_to_g, z_to_g;
    __m128 x_to_b, y_to_b, z_to_b;

    if (useP3) {
        // DisplayP3 XYZ to RGB matrix coefficients
        // Reference: SMPTE EG 432-1, Apple Display P3 specification
        x_to_r = _mm_set1_ps(2.4934969f);
        y_to_r = _mm_set1_ps(-0.93138361f);
        z_to_r = _mm_set1_ps(-0.40271077f);

        x_to_g = _mm_set1_ps(-0.82948899f);
        y_to_g = _mm_set1_ps(1.7626641f);
        z_to_g = _mm_set1_ps(0.02362468f);

        x_to_b = _mm_set1_ps(0.03584583f);
        y_to_b = _mm_set1_ps(-0.07617239f);
        z_to_b = _mm_set1_ps(0.9568845f);
    } else {
        // sRGB XYZ to RGB matrix (IEC 61966-2-1:2003 amendment)
        x_to_r = _mm_set1_ps(3.2406255f);
        y_to_r = _mm_set1_ps(-1.5372080f);
        z_to_r = _mm_set1_ps(-0.4986286f);

        x_to_g = _mm_set1_ps(-0.9689307f);
        y_to_g = _mm_set1_ps(1.8757561f);
        z_to_g = _mm_set1_ps(0.0415175f);

        x_to_b = _mm_set1_ps(0.0557101f);
        y_to_b = _mm_set1_ps(-0.2040211f);
        z_to_b = _mm_set1_ps(1.0569959f);
    }

    // sRGB gamma encoding constants
    const __m128 gammaThreshold = _mm_set1_ps(ColourMapper::SRGB_GAMMA_ENCODE_THRESHOLD);
    const __m128 gammaFactor = _mm_set1_ps(12.92f);
    const __m128 gammaMultiplier = _mm_set1_ps(1.055f);
    const __m128 gammaOffset = _mm_set1_ps(0.055f);
    const __m128 zero = _mm_set1_ps(0.0f);
    const __m128 one = _mm_set1_ps(1.0f);

    size_t i = 0;
    for (; i < vectorSize; i += 4) {
        __m128 XVec = _mm_loadu_ps(&X[i]);
        __m128 YVec = _mm_loadu_ps(&Y[i]);
        __m128 ZVec = _mm_loadu_ps(&Z[i]);

        // Matrix multiplication: RGB = M * XYZ
        __m128 r_linear = _mm_mul_ps(XVec, x_to_r);
        r_linear = _mm_add_ps(r_linear, _mm_mul_ps(YVec, y_to_r));
        r_linear = _mm_add_ps(r_linear, _mm_mul_ps(ZVec, z_to_r));

        __m128 g_linear = _mm_mul_ps(XVec, x_to_g);
        g_linear = _mm_add_ps(g_linear, _mm_mul_ps(YVec, y_to_g));
        g_linear = _mm_add_ps(g_linear, _mm_mul_ps(ZVec, z_to_g));

        __m128 b_linear = _mm_mul_ps(XVec, x_to_b);
        b_linear = _mm_add_ps(b_linear, _mm_mul_ps(YVec, y_to_b));
        b_linear = _mm_add_ps(b_linear, _mm_mul_ps(ZVec, z_to_b));

        // sRGB gamma encode: piecewise function
        // Linear region: linear <= 0.04045/12.92 → out = linear * 12.92
        // Power region: linear > 0.04045/12.92 → out = 1.055 * linear^(1/2.4) - 0.055

        // Threshold masks: true if linear <= threshold (use linear region)
        __m128 r_mask = _mm_cmple_ps(r_linear, gammaThreshold);
        __m128 g_mask = _mm_cmple_ps(g_linear, gammaThreshold);
        __m128 b_mask = _mm_cmple_ps(b_linear, gammaThreshold);

        // Linear region: linear * 12.92
        __m128 r_gamma_low = _mm_mul_ps(r_linear, gammaFactor);
        __m128 g_gamma_low = _mm_mul_ps(g_linear, gammaFactor);
        __m128 b_gamma_low = _mm_mul_ps(b_linear, gammaFactor);

        // Power region: 1.055 * linear^(1/2.4) - 0.055
        __m128 r_gamma_high = AccurateMath::accuratePow(r_linear, 1.0f / 2.4f);
        __m128 g_gamma_high = AccurateMath::accuratePow(g_linear, 1.0f / 2.4f);
        __m128 b_gamma_high = AccurateMath::accuratePow(b_linear, 1.0f / 2.4f);

        r_gamma_high = _mm_sub_ps(_mm_mul_ps(r_gamma_high, gammaMultiplier), gammaOffset);
        g_gamma_high = _mm_sub_ps(_mm_mul_ps(g_gamma_high, gammaMultiplier), gammaOffset);
        b_gamma_high = _mm_sub_ps(_mm_mul_ps(b_gamma_high, gammaMultiplier), gammaOffset);

        // Select between linear and power regions
        __m128 r_out = _mm_or_ps(_mm_and_ps(r_mask, r_gamma_low), _mm_andnot_ps(r_mask, r_gamma_high));
        __m128 g_out = _mm_or_ps(_mm_and_ps(g_mask, g_gamma_low), _mm_andnot_ps(g_mask, g_gamma_high));
        __m128 b_out = _mm_or_ps(_mm_and_ps(b_mask, b_gamma_low), _mm_andnot_ps(b_mask, b_gamma_high));

        // Clamp to [0, 1]
        r_out = _mm_max_ps(_mm_min_ps(r_out, one), zero);
        g_out = _mm_max_ps(_mm_min_ps(g_out, one), zero);
        b_out = _mm_max_ps(_mm_min_ps(b_out, one), zero);

        _mm_storeu_ps(&r[i], r_out);
        _mm_storeu_ps(&g[i], g_out);
        _mm_storeu_ps(&b[i], b_out);
    }

    // Scalar remainder loop
    for (; i < size; ++i) {
        float rOut, gOut, bOut;
        ColourMapper::XYZtoRGB(X[i], Y[i], Z[i], rOut, gOut, bOut, colourSpace, true, true);
        r[i] = std::clamp(rOut, 0.0f, 1.0f);
        g[i] = std::clamp(gOut, 0.0f, 1.0f);
        b[i] = std::clamp(bOut, 0.0f, 1.0f);
    }
}

void rgbToLab(std::span<const float> r, std::span<const float> g, std::span<const float> b,
              std::span<float> L, std::span<float> a, std::span<float> b_comp, size_t count,
              ColourSpace colourSpace) {
    std::vector<float> X_temp(count), Y_temp(count), Z_temp(count);

    rgbToXyz(r, g, b, X_temp, Y_temp, Z_temp, count, colourSpace);

    const size_t vectorSize = count & ~3u;

    // Reference white constants (D65, CIE 2006 2° observer)
    __m128 ref_x = _mm_set1_ps(synesthesia::constants::CIE_D65_REF_X);
    __m128 ref_y = _mm_set1_ps(synesthesia::constants::CIE_D65_REF_Y);
    __m128 ref_z = _mm_set1_ps(synesthesia::constants::CIE_D65_REF_Z);

    // Lab conversion constants
    __m128 epsilon = _mm_set1_ps(synesthesia::constants::LAB_EPSILON);
    __m128 kappa = _mm_set1_ps(synesthesia::constants::LAB_KAPPA);
    __m128 const_116 = _mm_set1_ps(116.0f);
    __m128 const_16 = _mm_set1_ps(16.0f);
    __m128 const_500 = _mm_set1_ps(500.0f);
    __m128 const_200 = _mm_set1_ps(200.0f);

    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 XVec = _mm_loadu_ps(&X_temp[i]);
        __m128 YVec = _mm_loadu_ps(&Y_temp[i]);
        __m128 ZVec = _mm_loadu_ps(&Z_temp[i]);

        __m128 xr = _mm_div_ps(XVec, ref_x);
        __m128 yr = _mm_div_ps(YVec, ref_y);
        __m128 zr = _mm_div_ps(ZVec, ref_z);

        // CIE LAB f() function: cube root for large values, linear for small
        // Using SSE: mask for epsilon threshold and blend results
        __m128 xr_cbrt = AccurateMath::accuratePow(xr, 1.0f / 3.0f);
        __m128 yr_cbrt = AccurateMath::accuratePow(yr, 1.0f / 3.0f);
        __m128 zr_cbrt = AccurateMath::accuratePow(zr, 1.0f / 3.0f);

        __m128 xr_linear = _mm_div_ps(_mm_add_ps(_mm_mul_ps(kappa, xr), const_16), const_116);
        __m128 yr_linear = _mm_div_ps(_mm_add_ps(_mm_mul_ps(kappa, yr), const_16), const_116);
        __m128 zr_linear = _mm_div_ps(_mm_add_ps(_mm_mul_ps(kappa, zr), const_16), const_116);

        // Select cube root or linear based on epsilon threshold
        __m128 xr_mask = _mm_cmpgt_ps(xr, epsilon);
        __m128 yr_mask = _mm_cmpgt_ps(yr, epsilon);
        __m128 zr_mask = _mm_cmpgt_ps(zr, epsilon);

        __m128 fx = _mm_or_ps(_mm_and_ps(xr_mask, xr_cbrt), _mm_andnot_ps(xr_mask, xr_linear));
        __m128 fy = _mm_or_ps(_mm_and_ps(yr_mask, yr_cbrt), _mm_andnot_ps(yr_mask, yr_linear));
        __m128 fz = _mm_or_ps(_mm_and_ps(zr_mask, zr_cbrt), _mm_andnot_ps(zr_mask, zr_linear));

        __m128 L_result = _mm_sub_ps(_mm_mul_ps(const_116, fy), const_16);
        __m128 a_result = _mm_mul_ps(const_500, _mm_sub_ps(fx, fy));
        __m128 b_result = _mm_mul_ps(const_200, _mm_sub_ps(fy, fz));

        L_result = _mm_min_ps(_mm_max_ps(L_result, _mm_setzero_ps()), _mm_set1_ps(100.0f));
        a_result = _mm_min_ps(_mm_max_ps(a_result, _mm_set1_ps(-128.0f)), _mm_set1_ps(127.0f));
        b_result = _mm_min_ps(_mm_max_ps(b_result, _mm_set1_ps(-128.0f)), _mm_set1_ps(127.0f));

        _mm_storeu_ps(&L[i], L_result);
        _mm_storeu_ps(&a[i], a_result);
        _mm_storeu_ps(&b_comp[i], b_result);
    }

    for (; i < count; ++i) {
        const float xr = X_temp[i] / 0.94755f;
        const float yr = Y_temp[i] / 1.0f;
        const float zr = Z_temp[i] / 1.07468f;

        auto f = [](float t) {
            constexpr float epsilon = 0.008856f;
            constexpr float kappa = 903.3f;
            return t > epsilon ? std::pow(t, 1.0f / 3.0f) : (kappa * t + 16.0f) / 116.0f;
        };

        const float fx = f(xr);
        const float fy = f(yr);
        const float fz = f(zr);

        L[i] = std::clamp(116.0f * fy - 16.0f, 0.0f, 100.0f);
        a[i] = std::clamp(500.0f * (fx - fy), -128.0f, 127.0f);
        b_comp[i] = std::clamp(200.0f * (fy - fz), -128.0f, 127.0f);
    }
}

void labToRgb(std::span<const float> L, std::span<const float> a, std::span<const float> b_comp,
              std::span<float> r, std::span<float> g, std::span<float> b, size_t count,
              ColourSpace colourSpace) {
    std::vector<float> X_temp(count), Y_temp(count), Z_temp(count);

    const size_t vectorSize = count & ~3u;

    // Reference white constants (D65, CIE 2006 2° observer)
    __m128 ref_x = _mm_set1_ps(synesthesia::constants::CIE_D65_REF_X);
    __m128 ref_y = _mm_set1_ps(synesthesia::constants::CIE_D65_REF_Y);
    __m128 ref_z = _mm_set1_ps(synesthesia::constants::CIE_D65_REF_Z);

    __m128 const_116 = _mm_set1_ps(116.0f);
    __m128 const_16 = _mm_set1_ps(16.0f);
    __m128 const_500 = _mm_set1_ps(500.0f);
    __m128 const_200 = _mm_set1_ps(200.0f);
    __m128 delta = _mm_set1_ps(synesthesia::constants::LAB_DELTA);

    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 L_vec = _mm_loadu_ps(&L[i]);
        __m128 a_vec = _mm_loadu_ps(&a[i]);
        __m128 b_vec = _mm_loadu_ps(&b_comp[i]);

        L_vec = _mm_min_ps(_mm_max_ps(L_vec, _mm_setzero_ps()), _mm_set1_ps(100.0f));
        a_vec = _mm_min_ps(_mm_max_ps(a_vec, _mm_set1_ps(-128.0f)), _mm_set1_ps(127.0f));
        b_vec = _mm_min_ps(_mm_max_ps(b_vec, _mm_set1_ps(-128.0f)), _mm_set1_ps(127.0f));

        __m128 fY = _mm_div_ps(_mm_add_ps(L_vec, const_16), const_116);
        __m128 fX = _mm_add_ps(fY, _mm_div_ps(a_vec, const_500));
        __m128 fZ = _mm_sub_ps(fY, _mm_div_ps(b_vec, const_200));

        // Inverse Lab f() function: cube for large values, linear for small
        __m128 fX_cube = AccurateMath::accuratePow(fX, 3.0f);
        __m128 fY_cube = AccurateMath::accuratePow(fY, 3.0f);
        __m128 fZ_cube = AccurateMath::accuratePow(fZ, 3.0f);

        constexpr float delta_val = synesthesia::constants::LAB_DELTA;
        __m128 delta_sq_3 = _mm_set1_ps(3.0f * delta_val * delta_val);
        __m128 const_4_29 = _mm_set1_ps(4.0f / 29.0f);

        __m128 fX_linear = _mm_mul_ps(delta_sq_3, _mm_sub_ps(fX, const_4_29));
        __m128 fY_linear = _mm_mul_ps(delta_sq_3, _mm_sub_ps(fY, const_4_29));
        __m128 fZ_linear = _mm_mul_ps(delta_sq_3, _mm_sub_ps(fZ, const_4_29));

        // Select cube or linear based on delta threshold
        __m128 fX_mask = _mm_cmpgt_ps(fX, delta);
        __m128 fY_mask = _mm_cmpgt_ps(fY, delta);
        __m128 fZ_mask = _mm_cmpgt_ps(fZ, delta);

        __m128 xr = _mm_or_ps(_mm_and_ps(fX_mask, fX_cube), _mm_andnot_ps(fX_mask, fX_linear));
        __m128 yr = _mm_or_ps(_mm_and_ps(fY_mask, fY_cube), _mm_andnot_ps(fY_mask, fY_linear));
        __m128 zr = _mm_or_ps(_mm_and_ps(fZ_mask, fZ_cube), _mm_andnot_ps(fZ_mask, fZ_linear));

        // Scale by reference white and clamp to positive
        __m128 X_result = _mm_max_ps(_mm_setzero_ps(), _mm_mul_ps(ref_x, xr));
        __m128 Y_result = _mm_max_ps(_mm_setzero_ps(), _mm_mul_ps(ref_y, yr));
        __m128 Z_result = _mm_max_ps(_mm_setzero_ps(), _mm_mul_ps(ref_z, zr));

        _mm_storeu_ps(&X_temp[i], X_result);
        _mm_storeu_ps(&Y_temp[i], Y_result);
        _mm_storeu_ps(&Z_temp[i], Z_result);
    }

    for (; i < count; ++i) {
        float L_val = std::clamp(L[i], 0.0f, 100.0f);
        float a_val = std::clamp(a[i], -128.0f, 127.0f);
        float b_val = std::clamp(b_comp[i], -128.0f, 127.0f);

        const float fY = (L_val + 16.0f) / 116.0f;
        const float fX = fY + a_val / 500.0f;
        const float fZ = fY - b_val / 200.0f;

        auto fInv = [](float t) {
            constexpr float delta = 6.0f / 29.0f;
            constexpr float delta_squared = delta * delta;
            return t > delta ? std::pow(t, 3.0f) : 3.0f * delta_squared * (t - 4.0f / 29.0f);
        };

        X_temp[i] = std::max(0.0f, 0.94755f * fInv(fX));
        Y_temp[i] = std::max(0.0f, 1.0f * fInv(fY));
        Z_temp[i] = std::max(0.0f, 1.07468f * fInv(fZ));
    }

    xyzToRgb(X_temp, Y_temp, Z_temp, r, g, b, count, colourSpace);
}

// Oklab: Björn Ottosson (2020) - https://bottosson.github.io/posts/oklab/
void rgbToOklab(std::span<const float> r, std::span<const float> g, std::span<const float> b,
                std::span<float> L, std::span<float> a, std::span<float> b_comp, size_t count,
                ColourSpace colourSpace) {
    const size_t size = std::min({r.size(), g.size(), b.size(), L.size(), a.size(), b_comp.size(), count});
    const size_t vectorSize = size & ~3u;

    // M1: RGB (linear) to LMS cone responses
    const __m128 m1_l_r = _mm_set1_ps(0.4122214708f);
    const __m128 m1_l_g = _mm_set1_ps(0.5363325363f);
    const __m128 m1_l_b = _mm_set1_ps(0.0514459929f);

    const __m128 m1_m_r = _mm_set1_ps(0.2119034982f);
    const __m128 m1_m_g = _mm_set1_ps(0.6806995451f);
    const __m128 m1_m_b = _mm_set1_ps(0.1073969566f);

    const __m128 m1_s_r = _mm_set1_ps(0.0883024619f);
    const __m128 m1_s_g = _mm_set1_ps(0.2817188376f);
    const __m128 m1_s_b = _mm_set1_ps(0.6299787005f);

    // M2: LMS' to Lab
    const __m128 m2_L_l = _mm_set1_ps(0.2104542553f);
    const __m128 m2_L_m = _mm_set1_ps(0.7936177850f);
    const __m128 m2_L_s = _mm_set1_ps(-0.0040720468f);

    const __m128 m2_a_l = _mm_set1_ps(1.9779984951f);
    const __m128 m2_a_m = _mm_set1_ps(-2.4285922050f);
    const __m128 m2_a_s = _mm_set1_ps(0.4505937099f);

    const __m128 m2_b_l = _mm_set1_ps(0.0259040371f);
    const __m128 m2_b_m = _mm_set1_ps(0.7827717662f);
    const __m128 m2_b_s = _mm_set1_ps(-0.8086757660f);

    const __m128 scale_100 = _mm_set1_ps(100.0f);
    const __m128 zero = _mm_setzero_ps();

    // Gamma decode parameters depend on colour space
    const bool isRec2020 = (colourSpace == ColourSpace::Rec2020);

    size_t i = 0;
    for (; i < vectorSize; i += 4) {
        __m128 rVec = _mm_loadu_ps(&r[i]);
        __m128 gVec = _mm_loadu_ps(&g[i]);
        __m128 bVec = _mm_loadu_ps(&b[i]);

        // Gamma decode (reuse logic from rgbToXyz)
        __m128 rLinear, gLinear, bLinear;

        if (isRec2020) {
            const float beta = 0.01805397f;
            const __m128 gammaThreshold = _mm_set1_ps(4.5f * beta);
            const __m128 linearSlopeInv = _mm_set1_ps(1.0f / 4.5f);

            const __m128 rMask = _mm_cmple_ps(rVec, gammaThreshold);
            const __m128 gMask = _mm_cmple_ps(gVec, gammaThreshold);
            const __m128 bMask = _mm_cmple_ps(bVec, gammaThreshold);

            const __m128 rLow = _mm_mul_ps(rVec, linearSlopeInv);
            const __m128 gLow = _mm_mul_ps(gVec, linearSlopeInv);
            const __m128 bLow = _mm_mul_ps(bVec, linearSlopeInv);

            const __m128 alphaVec = _mm_set1_ps(1.0992968f);
            const __m128 alphaMinusOne = _mm_set1_ps(0.0992968f);
            const __m128 rNorm = _mm_div_ps(_mm_add_ps(rVec, alphaMinusOne), alphaVec);
            const __m128 gNorm = _mm_div_ps(_mm_add_ps(gVec, alphaMinusOne), alphaVec);
            const __m128 bNorm = _mm_div_ps(_mm_add_ps(bVec, alphaMinusOne), alphaVec);

            __m128 rHigh = AccurateMath::accuratePow(rNorm, 1.0f / 0.45f);
            __m128 gHigh = AccurateMath::accuratePow(gNorm, 1.0f / 0.45f);
            __m128 bHigh = AccurateMath::accuratePow(bNorm, 1.0f / 0.45f);

            rLinear = _mm_or_ps(_mm_and_ps(rMask, rLow), _mm_andnot_ps(rMask, rHigh));
            gLinear = _mm_or_ps(_mm_and_ps(gMask, gLow), _mm_andnot_ps(gMask, gHigh));
            bLinear = _mm_or_ps(_mm_and_ps(bMask, bLow), _mm_andnot_ps(bMask, bHigh));
        } else {
            // sRGB/P3 gamma decode
            const __m128 gammaThreshold = _mm_set1_ps(0.04045f);
            const __m128 linearFactor = _mm_set1_ps(1.0f / 12.92f);
            const __m128 gammaOffset = _mm_set1_ps(0.055f);
            const __m128 gammaMultiplier = _mm_set1_ps(1.055f);

            const __m128 rMask = _mm_cmple_ps(rVec, gammaThreshold);
            const __m128 gMask = _mm_cmple_ps(gVec, gammaThreshold);
            const __m128 bMask = _mm_cmple_ps(bVec, gammaThreshold);

            const __m128 rLow = _mm_mul_ps(rVec, linearFactor);
            const __m128 gLow = _mm_mul_ps(gVec, linearFactor);
            const __m128 bLow = _mm_mul_ps(bVec, linearFactor);

            const __m128 rNorm = _mm_div_ps(_mm_add_ps(rVec, gammaOffset), gammaMultiplier);
            const __m128 gNorm = _mm_div_ps(_mm_add_ps(gVec, gammaOffset), gammaMultiplier);
            const __m128 bNorm = _mm_div_ps(_mm_add_ps(bVec, gammaOffset), gammaMultiplier);

            __m128 rHigh = AccurateMath::accuratePow(rNorm, 2.4f);
            __m128 gHigh = AccurateMath::accuratePow(gNorm, 2.4f);
            __m128 bHigh = AccurateMath::accuratePow(bNorm, 2.4f);

            rLinear = _mm_or_ps(_mm_and_ps(rMask, rLow), _mm_andnot_ps(rMask, rHigh));
            gLinear = _mm_or_ps(_mm_and_ps(gMask, gLow), _mm_andnot_ps(gMask, gHigh));
            bLinear = _mm_or_ps(_mm_and_ps(bMask, bLow), _mm_andnot_ps(bMask, bHigh));
        }

        // M1: RGB linear → LMS
        __m128 lms_l = _mm_mul_ps(rLinear, m1_l_r);
        lms_l = _mm_add_ps(lms_l, _mm_mul_ps(gLinear, m1_l_g));
        lms_l = _mm_add_ps(lms_l, _mm_mul_ps(bLinear, m1_l_b));

        __m128 lms_m = _mm_mul_ps(rLinear, m1_m_r);
        lms_m = _mm_add_ps(lms_m, _mm_mul_ps(gLinear, m1_m_g));
        lms_m = _mm_add_ps(lms_m, _mm_mul_ps(bLinear, m1_m_b));

        __m128 lms_s = _mm_mul_ps(rLinear, m1_s_r);
        lms_s = _mm_add_ps(lms_s, _mm_mul_ps(gLinear, m1_s_g));
        lms_s = _mm_add_ps(lms_s, _mm_mul_ps(bLinear, m1_s_b));

        // Clamp to non-negative before cube root
        lms_l = _mm_max_ps(lms_l, zero);
        lms_m = _mm_max_ps(lms_m, zero);
        lms_s = _mm_max_ps(lms_s, zero);

        // Cube root: x^(1/3)
        __m128 lms_l_cbrt = AccurateMath::accuratePow(lms_l, 1.0f / 3.0f);
        __m128 lms_m_cbrt = AccurateMath::accuratePow(lms_m, 1.0f / 3.0f);
        __m128 lms_s_cbrt = AccurateMath::accuratePow(lms_s, 1.0f / 3.0f);

        // M2: LMS' → Lab
        __m128 L_result = _mm_mul_ps(lms_l_cbrt, m2_L_l);
        L_result = _mm_add_ps(L_result, _mm_mul_ps(lms_m_cbrt, m2_L_m));
        L_result = _mm_add_ps(L_result, _mm_mul_ps(lms_s_cbrt, m2_L_s));

        __m128 a_result = _mm_mul_ps(lms_l_cbrt, m2_a_l);
        a_result = _mm_add_ps(a_result, _mm_mul_ps(lms_m_cbrt, m2_a_m));
        a_result = _mm_add_ps(a_result, _mm_mul_ps(lms_s_cbrt, m2_a_s));

        __m128 b_result = _mm_mul_ps(lms_l_cbrt, m2_b_l);
        b_result = _mm_add_ps(b_result, _mm_mul_ps(lms_m_cbrt, m2_b_m));
        b_result = _mm_add_ps(b_result, _mm_mul_ps(lms_s_cbrt, m2_b_s));

        // Scale and clamp
        L_result = _mm_mul_ps(L_result, scale_100);
        a_result = _mm_mul_ps(a_result, scale_100);
        b_result = _mm_mul_ps(b_result, scale_100);

        L_result = _mm_max_ps(_mm_min_ps(L_result, scale_100), zero);
        a_result = _mm_max_ps(_mm_min_ps(a_result, scale_100), _mm_sub_ps(zero, scale_100));
        b_result = _mm_max_ps(_mm_min_ps(b_result, scale_100), _mm_sub_ps(zero, scale_100));

        _mm_storeu_ps(&L[i], L_result);
        _mm_storeu_ps(&a[i], a_result);
        _mm_storeu_ps(&b_comp[i], b_result);
    }

    // Scalar remainder
    for (; i < size; ++i) {
        float L_val, a_val, b_val;
        ColourMapper::RGBtoOklab(r[i], g[i], b[i], L_val, a_val, b_val, colourSpace);
        L[i] = L_val;
        a[i] = a_val;
        b_comp[i] = b_val;
    }
}

void oklabToRgb(std::span<const float> L, std::span<const float> a, std::span<const float> b_comp,
                std::span<float> r, std::span<float> g, std::span<float> b, size_t count,
                ColourSpace colourSpace) {
    const size_t size = std::min({L.size(), a.size(), b_comp.size(), r.size(), g.size(), b.size(), count});
    const size_t vectorSize = size & ~3u;

    // M2^-1: Lab to LMS'
    const __m128 m2inv_l_L = _mm_set1_ps(1.0f);
    const __m128 m2inv_l_a = _mm_set1_ps(0.3963377774f);
    const __m128 m2inv_l_b = _mm_set1_ps(0.2158037573f);

    const __m128 m2inv_m_L = _mm_set1_ps(1.0f);
    const __m128 m2inv_m_a = _mm_set1_ps(-0.1055613458f);
    const __m128 m2inv_m_b = _mm_set1_ps(-0.0638541728f);

    const __m128 m2inv_s_L = _mm_set1_ps(1.0f);
    const __m128 m2inv_s_a = _mm_set1_ps(-0.0894841775f);
    const __m128 m2inv_s_b = _mm_set1_ps(-1.2914855480f);

    // M1^-1: LMS to RGB linear
    const __m128 m1inv_r_l = _mm_set1_ps(4.0767416621f);
    const __m128 m1inv_r_m = _mm_set1_ps(-3.3077115913f);
    const __m128 m1inv_r_s = _mm_set1_ps(0.2309699292f);

    const __m128 m1inv_g_l = _mm_set1_ps(-1.2684380046f);
    const __m128 m1inv_g_m = _mm_set1_ps(2.6097574011f);
    const __m128 m1inv_g_s = _mm_set1_ps(-0.3413193965f);

    const __m128 m1inv_b_l = _mm_set1_ps(-0.0041960863f);
    const __m128 m1inv_b_m = _mm_set1_ps(-0.7034186147f);
    const __m128 m1inv_b_s = _mm_set1_ps(1.7076147010f);

    const __m128 scale_inv = _mm_set1_ps(1.0f / 100.0f);
    const __m128 zero = _mm_setzero_ps();
    const __m128 one = _mm_set1_ps(1.0f);

    const bool isRec2020 = (colourSpace == ColourSpace::Rec2020);

    size_t i = 0;
    for (; i < vectorSize; i += 4) {
        __m128 L_vec = _mm_loadu_ps(&L[i]);
        __m128 a_vec = _mm_loadu_ps(&a[i]);
        __m128 b_vec = _mm_loadu_ps(&b_comp[i]);

        // Normalize and clamp
        L_vec = _mm_mul_ps(L_vec, scale_inv);
        a_vec = _mm_mul_ps(a_vec, scale_inv);
        b_vec = _mm_mul_ps(b_vec, scale_inv);

        L_vec = _mm_max_ps(_mm_min_ps(L_vec, one), zero);
        a_vec = _mm_max_ps(_mm_min_ps(a_vec, one), _mm_sub_ps(zero, one));
        b_vec = _mm_max_ps(_mm_min_ps(b_vec, one), _mm_sub_ps(zero, one));

        // M2^-1: Lab → LMS'
        __m128 lms_l_cbrt = _mm_mul_ps(L_vec, m2inv_l_L);
        lms_l_cbrt = _mm_add_ps(lms_l_cbrt, _mm_mul_ps(a_vec, m2inv_l_a));
        lms_l_cbrt = _mm_add_ps(lms_l_cbrt, _mm_mul_ps(b_vec, m2inv_l_b));

        __m128 lms_m_cbrt = _mm_mul_ps(L_vec, m2inv_m_L);
        lms_m_cbrt = _mm_add_ps(lms_m_cbrt, _mm_mul_ps(a_vec, m2inv_m_a));
        lms_m_cbrt = _mm_add_ps(lms_m_cbrt, _mm_mul_ps(b_vec, m2inv_m_b));

        __m128 lms_s_cbrt = _mm_mul_ps(L_vec, m2inv_s_L);
        lms_s_cbrt = _mm_add_ps(lms_s_cbrt, _mm_mul_ps(a_vec, m2inv_s_a));
        lms_s_cbrt = _mm_add_ps(lms_s_cbrt, _mm_mul_ps(b_vec, m2inv_s_b));

        // Cube: lms = lms'^3
        __m128 lms_l = AccurateMath::accuratePow(lms_l_cbrt, 3.0f);
        __m128 lms_m = AccurateMath::accuratePow(lms_m_cbrt, 3.0f);
        __m128 lms_s = AccurateMath::accuratePow(lms_s_cbrt, 3.0f);

        // M1^-1: LMS → RGB linear
        __m128 rLinear = _mm_mul_ps(lms_l, m1inv_r_l);
        rLinear = _mm_add_ps(rLinear, _mm_mul_ps(lms_m, m1inv_r_m));
        rLinear = _mm_add_ps(rLinear, _mm_mul_ps(lms_s, m1inv_r_s));

        __m128 gLinear = _mm_mul_ps(lms_l, m1inv_g_l);
        gLinear = _mm_add_ps(gLinear, _mm_mul_ps(lms_m, m1inv_g_m));
        gLinear = _mm_add_ps(gLinear, _mm_mul_ps(lms_s, m1inv_g_s));

        __m128 bLinear = _mm_mul_ps(lms_l, m1inv_b_l);
        bLinear = _mm_add_ps(bLinear, _mm_mul_ps(lms_m, m1inv_b_m));
        bLinear = _mm_add_ps(bLinear, _mm_mul_ps(lms_s, m1inv_b_s));

        // Clamp linear RGB
        rLinear = _mm_max_ps(_mm_min_ps(rLinear, one), zero);
        gLinear = _mm_max_ps(_mm_min_ps(gLinear, one), zero);
        bLinear = _mm_max_ps(_mm_min_ps(bLinear, one), zero);

        // Gamma encode (reuse logic from xyzToRgb)
        __m128 rOut, gOut, bOut;

        if (isRec2020) {
            const float beta = 0.01805397f;
            const float alpha = 1.0992968f;
            const __m128 betaVec = _mm_set1_ps(beta);
            const __m128 slope = _mm_set1_ps(4.5f);
            const __m128 alphaVec = _mm_set1_ps(alpha);
            const __m128 alphaMinusOne = _mm_set1_ps(alpha - 1.0f);

            const __m128 r_abs = _mm_andnot_ps(_mm_set1_ps(-0.0f), rLinear);
            const __m128 g_abs = _mm_andnot_ps(_mm_set1_ps(-0.0f), gLinear);
            const __m128 b_abs = _mm_andnot_ps(_mm_set1_ps(-0.0f), bLinear);

            const __m128 r_mask = _mm_cmple_ps(r_abs, betaVec);
            const __m128 g_mask = _mm_cmple_ps(g_abs, betaVec);
            const __m128 b_mask = _mm_cmple_ps(b_abs, betaVec);

            const __m128 r_low = _mm_mul_ps(rLinear, slope);
            const __m128 g_low = _mm_mul_ps(gLinear, slope);
            const __m128 b_low = _mm_mul_ps(bLinear, slope);

            __m128 r_high_mag = AccurateMath::accuratePow(r_abs, 0.45f);
            __m128 g_high_mag = AccurateMath::accuratePow(g_abs, 0.45f);
            __m128 b_high_mag = AccurateMath::accuratePow(b_abs, 0.45f);

            r_high_mag = _mm_sub_ps(_mm_mul_ps(alphaVec, r_high_mag), alphaMinusOne);
            g_high_mag = _mm_sub_ps(_mm_mul_ps(alphaVec, g_high_mag), alphaMinusOne);
            b_high_mag = _mm_sub_ps(_mm_mul_ps(alphaVec, b_high_mag), alphaMinusOne);

            const __m128 r_sign_mask = _mm_cmplt_ps(rLinear, zero);
            const __m128 g_sign_mask = _mm_cmplt_ps(gLinear, zero);
            const __m128 b_sign_mask = _mm_cmplt_ps(bLinear, zero);

            const __m128 r_high = _mm_or_ps(_mm_and_ps(r_sign_mask, _mm_sub_ps(zero, r_high_mag)),
                                            _mm_andnot_ps(r_sign_mask, r_high_mag));
            const __m128 g_high = _mm_or_ps(_mm_and_ps(g_sign_mask, _mm_sub_ps(zero, g_high_mag)),
                                            _mm_andnot_ps(g_sign_mask, g_high_mag));
            const __m128 b_high = _mm_or_ps(_mm_and_ps(b_sign_mask, _mm_sub_ps(zero, b_high_mag)),
                                            _mm_andnot_ps(b_sign_mask, b_high_mag));

            rOut = _mm_or_ps(_mm_and_ps(r_mask, r_low), _mm_andnot_ps(r_mask, r_high));
            gOut = _mm_or_ps(_mm_and_ps(g_mask, g_low), _mm_andnot_ps(g_mask, g_high));
            bOut = _mm_or_ps(_mm_and_ps(b_mask, b_low), _mm_andnot_ps(b_mask, b_high));
        } else {
            // sRGB/P3 gamma encode
            const __m128 gammaThreshold = _mm_set1_ps(ColourMapper::SRGB_GAMMA_ENCODE_THRESHOLD);
            const __m128 gammaFactor = _mm_set1_ps(12.92f);
            const __m128 gammaMultiplier = _mm_set1_ps(1.055f);
            const __m128 gammaOffset = _mm_set1_ps(0.055f);

            const __m128 r_mask = _mm_cmple_ps(rLinear, gammaThreshold);
            const __m128 g_mask = _mm_cmple_ps(gLinear, gammaThreshold);
            const __m128 b_mask = _mm_cmple_ps(bLinear, gammaThreshold);

            const __m128 r_gamma_low = _mm_mul_ps(rLinear, gammaFactor);
            const __m128 g_gamma_low = _mm_mul_ps(gLinear, gammaFactor);
            const __m128 b_gamma_low = _mm_mul_ps(bLinear, gammaFactor);

            __m128 r_gamma_high = AccurateMath::accuratePow(rLinear, 1.0f / 2.4f);
            __m128 g_gamma_high = AccurateMath::accuratePow(gLinear, 1.0f / 2.4f);
            __m128 b_gamma_high = AccurateMath::accuratePow(bLinear, 1.0f / 2.4f);

            r_gamma_high = _mm_sub_ps(_mm_mul_ps(r_gamma_high, gammaMultiplier), gammaOffset);
            g_gamma_high = _mm_sub_ps(_mm_mul_ps(g_gamma_high, gammaMultiplier), gammaOffset);
            b_gamma_high = _mm_sub_ps(_mm_mul_ps(b_gamma_high, gammaMultiplier), gammaOffset);

            rOut = _mm_or_ps(_mm_and_ps(r_mask, r_gamma_low), _mm_andnot_ps(r_mask, r_gamma_high));
            gOut = _mm_or_ps(_mm_and_ps(g_mask, g_gamma_low), _mm_andnot_ps(g_mask, g_gamma_high));
            bOut = _mm_or_ps(_mm_and_ps(b_mask, b_gamma_low), _mm_andnot_ps(b_mask, b_gamma_high));
        }

        // Final clamp to [0, 1]
        rOut = _mm_max_ps(_mm_min_ps(rOut, one), zero);
        gOut = _mm_max_ps(_mm_min_ps(gOut, one), zero);
        bOut = _mm_max_ps(_mm_min_ps(bOut, one), zero);

        _mm_storeu_ps(&r[i], rOut);
        _mm_storeu_ps(&g[i], gOut);
        _mm_storeu_ps(&b[i], bOut);
    }

    // Scalar remainder
    for (; i < size; ++i) {
        float r_val, g_val, b_val;
        ColourMapper::OklabtoRGB(L[i], a[i], b_comp[i], r_val, g_val, b_val, colourSpace);
        r[i] = r_val;
        g[i] = g_val;
        b[i] = b_val;
    }
}

void vectorLerp(std::span<float> result, std::span<const float> a, std::span<const float> b,
               std::span<const float> t, size_t count) {
    const size_t size = std::min({result.size(), a.size(), b.size(), t.size(), count});
    const size_t vectorSize = size & ~3u;

    __m128 one = _mm_set1_ps(1.0f);
    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 a_vec = _mm_loadu_ps(&a[i]);
        __m128 b_vec = _mm_loadu_ps(&b[i]);
        __m128 t_vec = _mm_loadu_ps(&t[i]);

        __m128 one_minus_t = _mm_sub_ps(one, t_vec);
        __m128 res = _mm_add_ps(_mm_mul_ps(a_vec, one_minus_t), _mm_mul_ps(b_vec, t_vec));
        _mm_storeu_ps(&result[i], res);
    }

    for (; i < size; ++i) {
        result[i] = a[i] * (1.0f - t[i]) + b[i] * t[i];
    }
}

void vectorClamp(std::span<float> data, float min_val, float max_val, size_t count) {
    const size_t size = std::min(data.size(), count);
    const size_t vectorSize = size & ~3u;

    __m128 minVec = _mm_set1_ps(min_val);
    __m128 maxVec = _mm_set1_ps(max_val);

    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 dataVec = _mm_loadu_ps(&data[i]);
        dataVec = _mm_min_ps(_mm_max_ps(dataVec, minVec), maxVec);
        _mm_storeu_ps(&data[i], dataVec);
    }

    for (; i < size; ++i) {
        data[i] = std::clamp(data[i], min_val, max_val);
    }
}

void vectorPow(std::span<float> data, float exponent, size_t count) {
    const size_t size = std::min(data.size(), count);
    const size_t vectorSize = size & ~3u;

    size_t i = 0;

    // Vectorized processing
    for (; i < vectorSize; i += 4) {
        __m128 dataVec = _mm_loadu_ps(&data[i]);
        __m128 resultVec = AccurateMath::accuratePow(dataVec, exponent);
        _mm_storeu_ps(&data[i], resultVec);
    }

    // Scalar remainder
    for (; i < size; ++i) {
        data[i] = std::pow(data[i], exponent);
    }
}

void vectorLog(std::span<float> result, std::span<const float> input, size_t count) {
    const size_t size = std::min({result.size(), input.size(), count});
    const size_t vectorSize = size & ~3u;

    size_t i = 0;

    // Vectorized processing using log2
    const float ln2 = 0.693147180559945309417f; // std::log(2.0f)
    __m128 ln2_vec = _mm_set1_ps(ln2);

    for (; i < vectorSize; i += 4) {
        __m128 inputVec = _mm_loadu_ps(&input[i]);
        __m128 log2Vec = AccurateMath::accurateLog2(inputVec);
        __m128 resultVec = _mm_mul_ps(log2Vec, ln2_vec); // log(x) = log2(x) * ln(2)
        _mm_storeu_ps(&result[i], resultVec);
    }

    // Scalar remainder
    for (; i < size; ++i) {
        result[i] = std::log(input[i]);
    }
}

void vectorExp(std::span<float> result, std::span<const float> input, size_t count) {
    const size_t size = std::min({result.size(), input.size(), count});
    const size_t vectorSize = size & ~3u;

    size_t i = 0;

    // Vectorized processing using exp2
    const float log2e = 1.44269504088896340736f; // std::log2(std::numbers::e)
    __m128 log2e_vec = _mm_set1_ps(log2e);

    for (; i < vectorSize; i += 4) {
        __m128 inputVec = _mm_loadu_ps(&input[i]);
        __m128 scaledInput = _mm_mul_ps(inputVec, log2e_vec); // exp(x) = 2^(x * log2(e))
        __m128 resultVec = AccurateMath::accurateExp2(scaledInput);
        _mm_storeu_ps(&result[i], resultVec);
    }

    // Scalar remainder
    for (; i < size; ++i) {
        result[i] = std::exp(input[i]);
    }
}

void vectorSqrt(std::span<float> result, std::span<const float> input, size_t count) {
    const size_t size = std::min({result.size(), input.size(), count});
    const size_t vectorSize = size & ~3u;

    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 inputVec = _mm_loadu_ps(&input[i]);
        __m128 resultVec = _mm_sqrt_ps(inputVec);
        _mm_storeu_ps(&result[i], resultVec);
    }

    for (; i < size; ++i) {
        result[i] = std::sqrt(input[i]);
    }
}

void enhanceColourWithAudioParams(
    float& r, float& g, float& b,
    const float loudness, const float spectralFlatness, const float spectralCentroid,
    const float spectralSpread, const float totalEnergy, const float spectralRolloff,
    const float spectralCrestFactor, const float spectralFlux, const bool onsetDetected,
    const ColourSpace colourSpace) {

    ColourMapper::enhanceColourWithAudioParams(
        r, g, b, loudness, spectralFlatness, spectralCentroid,
        spectralSpread, totalEnergy, spectralRolloff,
        spectralCrestFactor, spectralFlux, onsetDetected, colourSpace
    );
}

}

#endif

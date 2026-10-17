#pragma once

// https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffast-math
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ > 0)
#error "Colour smoothing requires IEEE NaN and infinity handling."
#endif

struct SmoothingSignalFeatures {
    bool onsetDetected = false;
    float spectralFlux = 0.0f;
    float spectralFlatness = 0.0f;
    float loudnessNormalised = 0.0f;
    float brightnessNormalised = 0.0f;
    float spectralSpreadNorm = 0.0f;
    float spectralRolloffNorm = 0.0f;
    float spectralCrestNorm = 0.0f;
    float phaseInstabilityNorm = 0.0f;
    float phaseCoherenceNorm = 0.0f;
    float phaseTransientNorm = 0.0f;
};

float manualResponseTime(float amount);
float manualSmoothingAmount(float seconds);

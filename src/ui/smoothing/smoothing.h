#pragma once

// https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffast-math
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ > 0)
#error "Colour smoothing requires IEEE NaN and infinity handling."
#endif

#include <limits>
#include <deque>
#include <utility>

struct SmoothingSignalFeatures {
    float frameLevelDb = std::numeric_limits<float>::quiet_NaN();
    double audioTimeSeconds = std::numeric_limits<double>::quiet_NaN();
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

class AudioResponseTracker {
public:
    float update(const SmoothingSignalFeatures& features, bool abruptAttack = false);
    void shiftLevel(double decibels);
    [[nodiscard]] bool attackDetected() const { return attackDetected_; }
private:
    std::deque<std::pair<double, double>> levels_;
    std::deque<std::pair<double, double>> modulationLevels_;
    double previousTime_ = std::numeric_limits<double>::quiet_NaN();
    double response_ = 0.4;
    double lastAttack_ = -std::numeric_limits<double>::infinity();
    double attackUntil_ = -std::numeric_limits<double>::infinity();
    double attackResponse_ = 0.08;
    bool previousOnset_ = false;
    bool previousAbruptAttack_ = false;
    bool earlyAttackPending_ = false;
    bool attackDetected_ = false;
    std::deque<double> intervals_;
};

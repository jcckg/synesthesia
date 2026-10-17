#pragma once

#include <array>
#include <limits>
#include <vector>
#include "spectral_presentation.h"

namespace SpectralPresentation {

class LogEnvelope {
public:
    struct Estimate { double magnitude = 0, power = 0, response = 0; };
    enum class Moment { Both, Magnitude, Power };
    struct Prediction {
        double dt, support, replacement;
        std::array<double, 9> levelVariance{}, covariance{}, rateVariance{};
        Prediction(double elapsed, double duration);
    };
    Estimate update(double power, double shape, double information, double dt, double support);
    Estimate update(double power, double shape, double information, const Prediction& prediction,
        Moment moment = Moment::Both, bool reportResponse = true);
private:
    struct State { double level = 0, rate = 0, p00 = 1, p01 = 0, p11 = 1, weight = 1.0 / 9; };
    std::array<State, 9> states_{};
    bool ready_ = false;
};

class SpectralEnvelope {
public:
    Frame process(const Frame& source, double timestamp, const Settings& settings = {});
    [[nodiscard]] double relativePowerVariance(std::size_t bin) const {
        return bin < bins_.size() ? 1 / bins_[bin].shape : 0;
    }
    [[nodiscard]] const std::vector<float>& responses() const { return responses_; }
private:
    struct Bin {
        LogEnvelope envelope;
        double phase = 0, velocity = 0, coherence = 0;
        double shape = 1;
        bool hasPhase = false, hasVelocity = false;
    };
    std::vector<Bin> bins_, weightedBins_, lineBins_, bassBins_;
    std::vector<float> responses_;
    double timestamp_ = std::numeric_limits<double>::quiet_NaN();
    double rate_ = 0;
    Frame previous_;
    LogEnvelope brightness_;
    std::array<float, 3> equaliser_{1, 1, 1};
    std::vector<double> brightnessWeights_;
    bool measuredBrightness_ = false;
    void filter(std::vector<float>& magnitudes, const std::vector<float>& phases,
        std::vector<Bin>& bins, double dt, double support, bool powerOutput,
        std::vector<float>* responses = nullptr, const std::vector<float>& shapes = {}, bool nyquist = true);
};

}

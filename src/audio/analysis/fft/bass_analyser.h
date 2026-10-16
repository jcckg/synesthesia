#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "real_fft.h"

namespace AudioAnalysis {
struct BassSpectrum {
    static constexpr std::size_t windowSize = 1024;
    float sampleRate = 0;
    float coverage = 0;
    std::vector<float> magnitudes;
};

class BassAnalyser {
public:
    void reset(float sourceRate = 0);
    void process(std::span<const float> samples, float sourceRate);
    std::shared_ptr<const BassSpectrum> spectrum();
private:
    RealFFT transform_{BassSpectrum::windowSize};
    float sourceRate_ = 0;
    std::size_t decimation_ = 1, phase_ = 0, position_ = 0;
    std::uint64_t outputs_ = 0, transformedOutputs_ = 0;
    std::vector<float> coefficients_, workspace_, decimated_;
    std::array<float,2048> history_{};
    std::array<float,1024> window_{}, input_{};
    std::array<kiss_fft_cpx,513> bins_{};
    std::shared_ptr<const BassSpectrum> spectrum_;
};
}

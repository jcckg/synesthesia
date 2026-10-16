#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include "kiss_fftr.h"

namespace AudioAnalysis {

enum class FFTBackend { Automatic, Kiss };

class RealFFT {
public:
    explicit RealFFT(std::size_t size, FFTBackend backend = FFTBackend::Automatic, bool inverse = false);
    ~RealFFT();
    RealFFT(const RealFFT&) = delete;
    RealFFT& operator=(const RealFFT&) = delete;
    void forward(std::span<const float> input, std::span<kiss_fft_cpx> output);
    void inverse(std::span<const kiss_fft_cpx> input, std::span<float> output);
    [[nodiscard]] bool accelerated() const;
private:
    struct Impl;
    std::unique_ptr<Impl> state_;
};

}

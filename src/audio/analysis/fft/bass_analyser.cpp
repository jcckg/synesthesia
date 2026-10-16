#include "bass_analyser.h"
#include <algorithm>
#include <cmath>
#include <numbers>

#if defined(__APPLE__) && defined(__aarch64__) && !defined(SYNESTHESIA_FORCE_KISS_FFT)
#include <arm_neon.h>
#endif

namespace AudioAnalysis {
namespace {
double besselI0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 100; ++k) {
        term *= x*x/(4.0*k*k);
        sum += term;
        if (term < sum*1e-16) break;
    }
    return sum;
}
}

// https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.firwin.html
void BassAnalyser::reset(float sourceRate) {
    sourceRate_ = sourceRate;
    decimation_ = 1;
    if (!std::isfinite(sourceRate_) || sourceRate_ < 8000 || sourceRate_ > 768000) { sourceRate_ = 0; spectrum_.reset(); return; }
    while (sourceRate_/static_cast<float>(decimation_*2) >= 2000) decimation_ *= 2;
    phase_ = position_ = 0;
    outputs_ = transformedOutputs_ = 0;
    history_.fill(0);
    spectrum_.reset();
    const auto taps = 32*decimation_+1;
    coefficients_.resize(taps);
    const double cutoff = .25/static_cast<double>(decimation_), beta = 8.6;
    const double denominator = besselI0(beta);
    double total = 0;
    for (std::size_t i = 0; i < taps; ++i) {
        const double offset = static_cast<double>(i)-static_cast<double>(taps-1)*.5;
        const double unit = offset/static_cast<double>((taps-1)/2);
        const double sinc = offset == 0 ? 2*cutoff : std::sin(2*std::numbers::pi*cutoff*offset)/(std::numbers::pi*offset);
        const double value = sinc*besselI0(beta*std::sqrt(std::max(0.0,1-unit*unit)))/denominator;
        coefficients_[i] = static_cast<float>(value);
        total += value;
    }
    for (auto& value : coefficients_) value = static_cast<float>(value/total);
    workspace_.assign(taps-1+1024,0);
    decimated_.resize(1024/decimation_+1);
    for (std::size_t i = 0; i < window_.size(); ++i)
        window_[i] = static_cast<float>(.5*(1-std::cos(2*std::numbers::pi*static_cast<double>(i)/1023)));
}

// https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.decimate.html
// https://arm-software.github.io/acle/neon_intrinsics/advsimd.html
void BassAnalyser::process(std::span<const float> samples, float sourceRate) {
    if (!std::isfinite(sourceRate) || sourceRate < 8000 || sourceRate > 768000) return;
    if (sourceRate_ != sourceRate) reset(sourceRate);
    while (!samples.empty()) {
        const auto count = std::min<std::size_t>(1024,samples.size()), preceding = coefficients_.size()-1;
        for (std::size_t i = 0; i < count; ++i) workspace_[preceding+i] = std::isfinite(samples[i]) ? samples[i] : 0;
        const auto first = decimation_-1-phase_;
        const auto produced = (phase_+count)/decimation_;
        if (produced > 0) {
#if defined(__APPLE__) && defined(__aarch64__) && !defined(SYNESTHESIA_FORCE_KISS_FFT)
            // Keep the reduction order independent of chunk length so batching cannot change filter results.
            for (std::size_t i = 0; i < produced; ++i) {
                const float* input = workspace_.data()+first+i*decimation_;
                float32x4_t sum = vdupq_n_f32(0);
                std::size_t tap = 0;
                for (; tap+4 <= coefficients_.size(); tap += 4)
                    sum = vaddq_f32(sum,vmulq_f32(vld1q_f32(input+tap),vld1q_f32(coefficients_.data()+tap)));
                std::array<float,4> lanes;
                vst1q_f32(lanes.data(),sum);
                float value = ((lanes[0]+lanes[1])+lanes[2])+lanes[3];
                for (; tap < coefficients_.size(); ++tap) value += input[tap]*coefficients_[tap];
                decimated_[i] = value;
            }
#else
            for (std::size_t i = 0; i < produced; ++i) {
                float sum = 0;
                for (std::size_t tap = 0; tap < coefficients_.size(); ++tap)
                    sum += workspace_[first+i*decimation_+tap]*coefficients_[tap];
                decimated_[i] = sum;
            }
#endif
            for (std::size_t i = 0; i < produced; ++i) {
                history_[position_] = history_[position_+1024] = decimated_[i];
                position_ = (position_+1)%1024;
            }
            outputs_ += produced;
        }
        phase_ = (phase_+count)%decimation_;
        std::copy_n(workspace_.begin()+static_cast<std::ptrdiff_t>(count),preceding,workspace_.begin());
        samples = samples.subspan(count);
    }
}

std::shared_ptr<const BassSpectrum> BassAnalyser::spectrum() {
    if (sourceRate_ == 0 || outputs_ == 0) return {};
    if (spectrum_ && transformedOutputs_ == outputs_) return spectrum_;
    for (std::size_t i = 0; i < 1024; ++i) input_[i] = history_[position_+i]*window_[i];
    transform_.forward(input_,bins_);
    auto result = std::make_shared<BassSpectrum>();
    result->sampleRate = sourceRate_/static_cast<float>(decimation_);
    result->coverage = std::min(1.0f,static_cast<float>(outputs_)/1024);
    const auto retained = std::min<std::size_t>(513,static_cast<std::size_t>(std::ceil(300*1024/result->sampleRate))+1);
    result->magnitudes.resize(retained);
    for (std::size_t i = 0; i < retained; ++i)
        result->magnitudes[i] = std::hypot(bins_[i].r,bins_[i].i)*(i == 0 ? 1.f/1024 : 2.f/1024);
    transformedOutputs_ = outputs_;
    return spectrum_ = std::move(result);
}
}

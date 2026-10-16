#include "real_fft.h"
#include <bit>
#include <stdexcept>
#include <vector>

#if defined(__APPLE__) && defined(__aarch64__) && !defined(SYNESTHESIA_FORCE_KISS_FFT)
#include <Accelerate/Accelerate.h>
#define SYNESTHESIA_ACCELERATE_FFT 1
#else
#define SYNESTHESIA_ACCELERATE_FFT 0
#endif

namespace AudioAnalysis {
struct RealFFT::Impl {
    std::size_t size;
    bool backwards;
    kiss_fftr_cfg kiss = nullptr;
#if SYNESTHESIA_ACCELERATE_FFT
    FFTSetup setup = nullptr;
    vDSP_Length exponent = 0;
    std::vector<float> real, imaginary;
#endif
    Impl(std::size_t length, FFTBackend backend, bool inverse) : size(length), backwards(inverse) {
        if (size < 2 || size % 2 != 0 || size > (1u << 24)) throw std::invalid_argument("Unsupported real FFT length");
#if SYNESTHESIA_ACCELERATE_FFT
        if (backend == FFTBackend::Automatic && std::has_single_bit(size)) {
            exponent = std::countr_zero(size);
            real.resize(size/2); imaginary.resize(size/2);
            setup = vDSP_create_fftsetup(exponent,kFFTRadix2);
            if (!setup) throw std::bad_alloc();
            return;
        }
#else
        (void)backend;
#endif
        kiss = kiss_fftr_alloc(static_cast<int>(size),inverse ? 1 : 0,nullptr,nullptr);
        if (!kiss) throw std::bad_alloc();
    }
    ~Impl() {
        if (kiss) kiss_fftr_free(kiss);
#if SYNESTHESIA_ACCELERATE_FFT
        if (setup) vDSP_destroy_fftsetup(setup);
#endif
    }
};
RealFFT::RealFFT(std::size_t size, FFTBackend backend, bool inverse) : state_(std::make_unique<Impl>(size,backend,inverse)) {}
RealFFT::~RealFFT() = default;
bool RealFFT::accelerated() const {
#if SYNESTHESIA_ACCELERATE_FFT
    return state_->setup != nullptr;
#else
    return false;
#endif
}

// https://developer.apple.com/library/archive/documentation/Performance/Conceptual/vDSP_Programming_Guide/UsingFourierTransforms/UsingFourierTransforms.html
void RealFFT::forward(std::span<const float> input, std::span<kiss_fft_cpx> output) {
    auto& s = *state_;
    if (s.backwards || input.size() != s.size || output.size() != s.size/2+1) throw std::invalid_argument("Invalid forward FFT buffers");
#if SYNESTHESIA_ACCELERATE_FFT
    if (s.setup) {
        // vDSP packs Nyquist into DC's imaginary slot and returns twice the conventional real DFT.
        DSPSplitComplex split{s.real.data(),s.imaginary.data()};
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(input.data()),2,&split,1,s.size/2);
        vDSP_fft_zrip(s.setup,&split,1,s.exponent,FFT_FORWARD);
        output[0] = {s.real[0]*.5f,0};
        output[s.size/2] = {s.imaginary[0]*.5f,0};
        for (std::size_t i = 1; i < s.size/2; ++i) output[i] = {s.real[i]*.5f,s.imaginary[i]*.5f};
        return;
    }
#endif
    kiss_fftr(s.kiss,input.data(),output.data());
}

// https://developer.apple.com/library/archive/documentation/Performance/Conceptual/vDSP_Programming_Guide/UsingFourierTransforms/UsingFourierTransforms.html
void RealFFT::inverse(std::span<const kiss_fft_cpx> input, std::span<float> output) {
    auto& s = *state_;
    if (!s.backwards || input.size() != s.size/2+1 || output.size() != s.size) throw std::invalid_argument("Invalid inverse FFT buffers");
#if SYNESTHESIA_ACCELERATE_FFT
    if (s.setup) {
        // Repack the endpoints; the inverse intentionally returns N times the signal to match KISS.
        s.real[0] = input[0].r;
        s.imaginary[0] = input[s.size/2].r;
        for (std::size_t i = 1; i < s.size/2; ++i) { s.real[i] = input[i].r; s.imaginary[i] = input[i].i; }
        DSPSplitComplex split{s.real.data(),s.imaginary.data()};
        vDSP_fft_zrip(s.setup,&split,1,s.exponent,FFT_INVERSE);
        vDSP_ztoc(&split,1,reinterpret_cast<DSPComplex*>(output.data()),2,s.size/2);
        return;
    }
#endif
    kiss_fftri(s.kiss,input.data(),output.data());
}
}

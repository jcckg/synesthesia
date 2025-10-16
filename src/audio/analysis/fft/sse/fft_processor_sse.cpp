#include "fft_processor_sse.h"

#if defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)

#include <algorithm>
#include <cmath>

namespace FFTProcessorSSE {

bool isSSEAvailable() {
    return true;
}

void applyHannWindow(std::span<float> output, std::span<const float> input,
                    std::span<const float> window) {
    const size_t size = std::min({output.size(), input.size(), window.size()});
    const size_t vectorSize = size & ~3u;

    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 inputVec = _mm_loadu_ps(&input[i]);
        __m128 windowVec = _mm_loadu_ps(&window[i]);
        __m128 result = _mm_mul_ps(inputVec, windowVec);
        _mm_storeu_ps(&output[i], result);
    }

    for (; i < size; ++i) {
        output[i] = input[i] * window[i];
    }
}

void calculateMagnitudes(std::span<float> magnitudes, std::span<const float> real,
                        std::span<const float> imag) {
    const size_t size = std::min({magnitudes.size(), real.size(), imag.size()});
    const size_t vectorSize = size & ~3u;

    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 realVec = _mm_loadu_ps(&real[i]);
        __m128 imagVec = _mm_loadu_ps(&imag[i]);

        __m128 realSq = _mm_mul_ps(realVec, realVec);
        __m128 imagSq = _mm_mul_ps(imagVec, imagVec);
        __m128 sumSq = _mm_add_ps(realSq, imagSq);

        __m128 magnitude = _mm_sqrt_ps(sumSq);
        _mm_storeu_ps(&magnitudes[i], magnitude);
    }

    for (; i < size; ++i) {
        magnitudes[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
    }
}

void calculateSpectralEnergy(std::span<float> envelope, std::span<const float> real,
                           std::span<const float> imag, float totalEnergyInv) {
    const size_t size = std::min({envelope.size(), real.size(), imag.size()});
    const size_t vectorSize = size & ~3u;

    __m128 totalEnergyInvVec = _mm_set1_ps(totalEnergyInv);
    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 realVec = _mm_loadu_ps(&real[i]);
        __m128 imagVec = _mm_loadu_ps(&imag[i]);

        __m128 realSq = _mm_mul_ps(realVec, realVec);
        __m128 imagSq = _mm_mul_ps(imagVec, imagVec);
        __m128 energy = _mm_add_ps(realSq, imagSq);

        __m128 normalisedEnergy = _mm_mul_ps(energy, totalEnergyInvVec);
        _mm_storeu_ps(&envelope[i], normalisedEnergy);
    }

    for (; i < size; ++i) {
        float energy = real[i] * real[i] + imag[i] * imag[i];
        envelope[i] = energy * totalEnergyInv;
    }
}

void applyEQGains(std::span<float> magnitudes, std::span<const float> frequencies,
                 float lowGain, float midGain, float highGain,
                 float /* sampleRate */, size_t minBin, size_t maxBin) {
    const size_t size = std::min(magnitudes.size(), frequencies.size());
    if (maxBin >= size) maxBin = size - 1;
    if (minBin > maxBin) return;

    __m128 lowGainVec = _mm_set1_ps(lowGain);
    __m128 midGainVec = _mm_set1_ps(midGain);
    __m128 highGainVec = _mm_set1_ps(highGain);
    __m128 freq200Vec = _mm_set1_ps(200.0f);
    __m128 freq1900Vec = _mm_set1_ps(1900.0f);
    __m128 oneVec = _mm_set1_ps(1.0f);
    __m128 zeroVec = _mm_setzero_ps();

    const size_t vectorStart = minBin;
    const size_t vectorEnd = maxBin & ~3u;

    size_t i = vectorStart;

    for (; i < vectorEnd && i + 4 <= maxBin; i += 4) {
        __m128 freqVec = _mm_loadu_ps(&frequencies[i]);
        __m128 magVec = _mm_loadu_ps(&magnitudes[i]);

        __m128 lowTemp = _mm_sub_ps(freqVec, freq200Vec);
        lowTemp = _mm_mul_ps(lowTemp, _mm_set1_ps(1.0f / 50.0f));
        lowTemp = _mm_max_ps(lowTemp, zeroVec);
        __m128 lowResponse = _mm_sub_ps(oneVec, lowTemp);
        lowResponse = _mm_max_ps(lowResponse, zeroVec);
        lowResponse = _mm_min_ps(lowResponse, oneVec);

        __m128 highTemp = _mm_sub_ps(freqVec, freq1900Vec);
        __m128 highResponse = _mm_mul_ps(highTemp, _mm_set1_ps(1.0f / 100.0f));
        highResponse = _mm_max_ps(highResponse, zeroVec);
        highResponse = _mm_min_ps(highResponse, oneVec);

        __m128 midResponse = _mm_sub_ps(oneVec, lowResponse);
        midResponse = _mm_sub_ps(midResponse, highResponse);
        midResponse = _mm_max_ps(midResponse, zeroVec);
        midResponse = _mm_min_ps(midResponse, oneVec);

        __m128 lowContrib = _mm_mul_ps(lowResponse, lowGainVec);
        __m128 midContrib = _mm_mul_ps(midResponse, midGainVec);
        __m128 highContrib = _mm_mul_ps(highResponse, highGainVec);

        __m128 totalGain = _mm_add_ps(lowContrib, midContrib);
        totalGain = _mm_add_ps(totalGain, highContrib);

        __m128 result = _mm_mul_ps(magVec, totalGain);
        _mm_storeu_ps(&magnitudes[i], result);
    }

    for (; i <= maxBin; ++i) {
        float freq = frequencies[i];

        float lowResponse = std::clamp(1.0f - std::max(0.0f, (freq - 200.0f) / 50.0f), 0.0f, 1.0f);
        float highResponse = std::clamp((freq - 1900.0f) / 100.0f, 0.0f, 1.0f);
        float midResponse = std::clamp(1.0f - lowResponse - highResponse, 0.0f, 1.0f);

        float combinedGain = lowResponse * lowGain + midResponse * midGain + highResponse * highGain;
        magnitudes[i] *= combinedGain;
    }
}

void vectorMultiply(std::span<float> result, std::span<const float> a, std::span<const float> b) {
    const size_t size = std::min({result.size(), a.size(), b.size()});
    const size_t vectorSize = size & ~3u;

    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 aVec = _mm_loadu_ps(&a[i]);
        __m128 bVec = _mm_loadu_ps(&b[i]);
        __m128 resultVec = _mm_mul_ps(aVec, bVec);
        _mm_storeu_ps(&result[i], resultVec);
    }

    for (; i < size; ++i) {
        result[i] = a[i] * b[i];
    }
}

void vectorScale(std::span<float> data, float scale) {
    const size_t size = data.size();
    const size_t vectorSize = size & ~3u;

    __m128 scaleVec = _mm_set1_ps(scale);
    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 dataVec = _mm_loadu_ps(&data[i]);
        __m128 result = _mm_mul_ps(dataVec, scaleVec);
        _mm_storeu_ps(&data[i], result);
    }

    for (; i < size; ++i) {
        data[i] *= scale;
    }
}

void vectorFill(std::span<float> data, float value) {
    const size_t size = data.size();
    const size_t vectorSize = size & ~3u;

    __m128 valueVec = _mm_set1_ps(value);
    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        _mm_storeu_ps(&data[i], valueVec);
    }

    for (; i < size; ++i) {
        data[i] = value;
    }
}

float vectorSum(std::span<const float> data) {
    const size_t size = data.size();
    const size_t vectorSize = size & ~3u;

    __m128 sumVec = _mm_setzero_ps();
    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 dataVec = _mm_loadu_ps(&data[i]);
        sumVec = _mm_add_ps(sumVec, dataVec);
    }

    __m128 shuffled = _mm_shuffle_ps(sumVec, sumVec, _MM_SHUFFLE(2, 3, 0, 1));
    sumVec = _mm_add_ps(sumVec, shuffled);
    shuffled = _mm_shuffle_ps(sumVec, sumVec, _MM_SHUFFLE(1, 0, 3, 2));
    sumVec = _mm_add_ps(sumVec, shuffled);
    float sum = _mm_cvtss_f32(sumVec);

    for (; i < size; ++i) {
        sum += data[i];
    }

    return sum;
}

float vectorMax(std::span<const float> data) {
    if (data.empty()) return 0.0f;

    const size_t size = data.size();
    const size_t vectorSize = size & ~3u;

    __m128 maxVec = _mm_set1_ps(data[0]);
    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 dataVec = _mm_loadu_ps(&data[i]);
        maxVec = _mm_max_ps(maxVec, dataVec);
    }

    __m128 shuffled = _mm_shuffle_ps(maxVec, maxVec, _MM_SHUFFLE(2, 3, 0, 1));
    maxVec = _mm_max_ps(maxVec, shuffled);
    shuffled = _mm_shuffle_ps(maxVec, maxVec, _MM_SHUFFLE(1, 0, 3, 2));
    maxVec = _mm_max_ps(maxVec, shuffled);
    float maxVal = _mm_cvtss_f32(maxVec);

    for (; i < size; ++i) {
        maxVal = std::max(maxVal, data[i]);
    }

    return maxVal;
}

void calculateMagnitudesFromComplex(std::span<float> magnitudes,
                                   const kiss_fft_cpx* fft_output, size_t count) {
    const size_t size = std::min(magnitudes.size(), count);
    const size_t vectorSize = size & ~3u;
    size_t i = 0;

    for (; i < vectorSize; i += 4) {
        __m128 real_vals = _mm_set_ps(fft_output[i+3].r, fft_output[i+2].r,
                                      fft_output[i+1].r, fft_output[i].r);
        __m128 imag_vals = _mm_set_ps(fft_output[i+3].i, fft_output[i+2].i,
                                      fft_output[i+1].i, fft_output[i].i);

        __m128 realSq = _mm_mul_ps(real_vals, real_vals);
        __m128 imagSq = _mm_mul_ps(imag_vals, imag_vals);
        __m128 sum = _mm_add_ps(realSq, imagSq);
        __m128 result = _mm_sqrt_ps(sum);

        _mm_storeu_ps(&magnitudes[i], result);
    }

    for (; i < size; ++i) {
        const float real = fft_output[i].r;
        const float imag = fft_output[i].i;
        magnitudes[i] = std::sqrt(real * real + imag * imag);
    }
}

}

#endif

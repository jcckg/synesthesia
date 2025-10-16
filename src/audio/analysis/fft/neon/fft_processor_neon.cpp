#include "fft_processor_neon.h"

#ifdef __ARM_NEON

#include <algorithm>
#include <cmath>

namespace FFTProcessorNEON {

bool isNEONAvailable() {
    return true;
}

void applyHannWindow(std::span<float> output, std::span<const float> input, 
                    std::span<const float> window) {
    const size_t size = std::min({output.size(), input.size(), window.size()});
    const size_t vectorSize = size & ~3u;
    
    size_t i = 0;
    
    for (; i < vectorSize; i += 4) {
        float32x4_t inputVec = vld1q_f32(&input[i]);
        float32x4_t windowVec = vld1q_f32(&window[i]);
        float32x4_t result = vmulq_f32(inputVec, windowVec);
        vst1q_f32(&output[i], result);
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
        float32x4_t realVec = vld1q_f32(&real[i]);
        float32x4_t imagVec = vld1q_f32(&imag[i]);
        
        float32x4_t realSq = vmulq_f32(realVec, realVec);
        float32x4_t imagSq = vmulq_f32(imagVec, imagVec);
        float32x4_t sumSq = vaddq_f32(realSq, imagSq);
        
        float32x4_t magnitude = vsqrtq_f32(sumSq);
        vst1q_f32(&magnitudes[i], magnitude);
    }
    
    for (; i < size; ++i) {
        magnitudes[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
    }
}

void calculateSpectralEnergy(std::span<float> envelope, std::span<const float> real, 
                           std::span<const float> imag, float totalEnergyInv) {
    const size_t size = std::min({envelope.size(), real.size(), imag.size()});
    const size_t vectorSize = size & ~3u;
    
    float32x4_t totalEnergyInvVec = vdupq_n_f32(totalEnergyInv);
    size_t i = 0;
    
    for (; i < vectorSize; i += 4) {
        float32x4_t realVec = vld1q_f32(&real[i]);
        float32x4_t imagVec = vld1q_f32(&imag[i]);
        
        float32x4_t realSq = vmulq_f32(realVec, realVec);
        float32x4_t imagSq = vmulq_f32(imagVec, imagVec);
        float32x4_t energy = vaddq_f32(realSq, imagSq);
        
        float32x4_t normalisedEnergy = vmulq_f32(energy, totalEnergyInvVec);
        vst1q_f32(&envelope[i], normalisedEnergy);
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
    
    float32x4_t lowGainVec = vdupq_n_f32(lowGain);
    float32x4_t midGainVec = vdupq_n_f32(midGain);
    float32x4_t highGainVec = vdupq_n_f32(highGain);
    float32x4_t freq200Vec = vdupq_n_f32(200.0f);
    float32x4_t freq1900Vec = vdupq_n_f32(1900.0f);
    float32x4_t oneVec = vdupq_n_f32(1.0f);
    float32x4_t zeroVec = vdupq_n_f32(0.0f);
    
    const size_t vectorStart = minBin;
    const size_t vectorEnd = maxBin & ~3u;
    
    size_t i = vectorStart;
    
    for (; i < vectorEnd && i + 4 <= maxBin; i += 4) {
        float32x4_t freqVec = vld1q_f32(&frequencies[i]);
        float32x4_t magVec = vld1q_f32(&magnitudes[i]);
        
        float32x4_t lowTemp = vsubq_f32(freqVec, freq200Vec);
        lowTemp = vmulq_f32(lowTemp, vdupq_n_f32(1.0f / 50.0f));
        lowTemp = vmaxq_f32(lowTemp, zeroVec);
        float32x4_t lowResponse = vsubq_f32(oneVec, lowTemp);
        lowResponse = vmaxq_f32(lowResponse, zeroVec);
        lowResponse = vminq_f32(lowResponse, oneVec);
        
        float32x4_t highTemp = vsubq_f32(freqVec, freq1900Vec);
        float32x4_t highResponse = vmulq_f32(highTemp, vdupq_n_f32(1.0f / 100.0f));
        highResponse = vmaxq_f32(highResponse, zeroVec);
        highResponse = vminq_f32(highResponse, oneVec);
        
        float32x4_t midResponse = vsubq_f32(oneVec, lowResponse);
        midResponse = vsubq_f32(midResponse, highResponse);
        midResponse = vmaxq_f32(midResponse, zeroVec);
        midResponse = vminq_f32(midResponse, oneVec);
        
        float32x4_t lowContrib = vmulq_f32(lowResponse, lowGainVec);
        float32x4_t midContrib = vmulq_f32(midResponse, midGainVec);
        float32x4_t highContrib = vmulq_f32(highResponse, highGainVec);
        
        float32x4_t totalGain = vaddq_f32(lowContrib, midContrib);
        totalGain = vaddq_f32(totalGain, highContrib);
        
        float32x4_t result = vmulq_f32(magVec, totalGain);
        vst1q_f32(&magnitudes[i], result);
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
        float32x4_t aVec = vld1q_f32(&a[i]);
        float32x4_t bVec = vld1q_f32(&b[i]);
        float32x4_t resultVec = vmulq_f32(aVec, bVec);
        vst1q_f32(&result[i], resultVec);
    }
    
    for (; i < size; ++i) {
        result[i] = a[i] * b[i];
    }
}

void vectorScale(std::span<float> data, float scale) {
    const size_t size = data.size();
    const size_t vectorSize = size & ~3u;
    
    float32x4_t scaleVec = vdupq_n_f32(scale);
    size_t i = 0;
    
    for (; i < vectorSize; i += 4) {
        float32x4_t dataVec = vld1q_f32(&data[i]);
        float32x4_t result = vmulq_f32(dataVec, scaleVec);
        vst1q_f32(&data[i], result);
    }
    
    for (; i < size; ++i) {
        data[i] *= scale;
    }
}

void vectorFill(std::span<float> data, float value) {
    const size_t size = data.size();
    const size_t vectorSize = size & ~3u;
    
    float32x4_t valueVec = vdupq_n_f32(value);
    size_t i = 0;
    
    for (; i < vectorSize; i += 4) {
        vst1q_f32(&data[i], valueVec);
    }
    
    for (; i < size; ++i) {
        data[i] = value;
    }
}

float vectorSum(std::span<const float> data) {
    const size_t size = data.size();
    const size_t vectorSize = size & ~3u;
    
    float32x4_t sumVec = vdupq_n_f32(0.0f);
    size_t i = 0;
    
    for (; i < vectorSize; i += 4) {
        float32x4_t dataVec = vld1q_f32(&data[i]);
        sumVec = vaddq_f32(sumVec, dataVec);
    }
    
    float32x2_t sum_low = vget_low_f32(sumVec);
    float32x2_t sum_high = vget_high_f32(sumVec);
    float32x2_t sum_pair = vadd_f32(sum_low, sum_high);
    float sum = vget_lane_f32(vpadd_f32(sum_pair, sum_pair), 0);
    
    for (; i < size; ++i) {
        sum += data[i];
    }
    
    return sum;
}

float vectorMax(std::span<const float> data) {
    if (data.empty()) return 0.0f;
    
    const size_t size = data.size();
    const size_t vectorSize = size & ~3u;
    
    float32x4_t maxVec = vdupq_n_f32(data[0]);
    size_t i = 0;
    
    for (; i < vectorSize; i += 4) {
        float32x4_t dataVec = vld1q_f32(&data[i]);
        maxVec = vmaxq_f32(maxVec, dataVec);
    }
    
    float32x2_t max_low = vget_low_f32(maxVec);
    float32x2_t max_high = vget_high_f32(maxVec);
    float32x2_t max_pair = vmax_f32(max_low, max_high);
    float maxVal = vget_lane_f32(vpmax_f32(max_pair, max_pair), 0);
    
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
        float32x4_t real_vals = {fft_output[i].r, fft_output[i+1].r, fft_output[i+2].r, fft_output[i+3].r};
        float32x4_t imag_vals = {fft_output[i].i, fft_output[i+1].i, fft_output[i+2].i, fft_output[i+3].i};
        
        float32x4_t realSq = vmulq_f32(real_vals, real_vals);
        float32x4_t imagSq = vmulq_f32(imag_vals, imag_vals);
        float32x4_t sum = vaddq_f32(realSq, imagSq);
        float32x4_t sqrtApprox = vrsqrteq_f32(sum);
        sqrtApprox = vmulq_f32(sqrtApprox, vrsqrtsq_f32(vmulq_f32(sum, sqrtApprox), sqrtApprox));
        float32x4_t result = vmulq_f32(sum, sqrtApprox);
        
        vst1q_f32(&magnitudes[i], result);
    }
    
    for (; i < size; ++i) {
        const float real = fft_output[i].r;
        const float imag = fft_output[i].i;
        magnitudes[i] = std::sqrt(real * real + imag * imag);
    }
}

}

#endif

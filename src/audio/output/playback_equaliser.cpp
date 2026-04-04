#include "playback_equaliser.h"

#include <algorithm>
#include <cmath>

#include "audio/analysis/eq/shared_eq_model.h"

void PlaybackEqualiser::configure(const float sampleRate, const size_t channels) {
    channelStates_.assign(std::max<size_t>(1, channels), {});
    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    updateCoefficients(
        sampleRate,
        lowGain_.load(std::memory_order_relaxed),
        midGain_.load(std::memory_order_relaxed),
        highGain_.load(std::memory_order_relaxed));
    resetRequested_.store(true, std::memory_order_relaxed);
}

void PlaybackEqualiser::setEnabled(const bool enabled) {
    const bool previous = enabled_.exchange(enabled, std::memory_order_relaxed);
    if (previous != enabled) {
        resetRequested_.store(true, std::memory_order_relaxed);
    }
}

void PlaybackEqualiser::setGains(const float low, const float mid, const float high) {
    const float clampedLow = std::max(0.0f, low);
    const float clampedMid = std::max(0.0f, mid);
    const float clampedHigh = std::max(0.0f, high);
    const float previousLow = lowGain_.load(std::memory_order_relaxed);
    const float previousMid = midGain_.load(std::memory_order_relaxed);
    const float previousHigh = highGain_.load(std::memory_order_relaxed);
    const bool unchanged =
        std::abs(previousLow - clampedLow) < 1e-6f &&
        std::abs(previousMid - clampedMid) < 1e-6f &&
        std::abs(previousHigh - clampedHigh) < 1e-6f;
    if (unchanged) {
        return;
    }
    lowGain_.store(clampedLow, std::memory_order_relaxed);
    midGain_.store(clampedMid, std::memory_order_relaxed);
    highGain_.store(clampedHigh, std::memory_order_relaxed);
    updateCoefficients(sampleRate_.load(std::memory_order_relaxed), clampedLow, clampedMid, clampedHigh);
}

void PlaybackEqualiser::requestReset() {
    resetRequested_.store(true, std::memory_order_relaxed);
}

void PlaybackEqualiser::processInterleaved(float* buffer, const size_t frames, const size_t channels) {
    if (buffer == nullptr || frames == 0 || channels == 0) {
        return;
    }

    if (resetRequested_.exchange(false, std::memory_order_relaxed)) {
        for (auto& state : channelStates_) {
            state = {};
        }
    }

    if (!enabled_.load(std::memory_order_relaxed)) {
        return;
    }

    if (channelStates_.size() != channels) {
        channelStates_.assign(channels, {});
    }

    const bool neutral =
        std::abs(lowGain_.load(std::memory_order_relaxed) - 1.0f) < 1e-6f &&
        std::abs(midGain_.load(std::memory_order_relaxed) - 1.0f) < 1e-6f &&
        std::abs(highGain_.load(std::memory_order_relaxed) - 1.0f) < 1e-6f;
    if (neutral) {
        return;
    }

    const AudioEQ::BiquadCoefficients low{
        lowB0_.load(std::memory_order_relaxed),
        lowB1_.load(std::memory_order_relaxed),
        lowB2_.load(std::memory_order_relaxed),
        lowA1_.load(std::memory_order_relaxed),
        lowA2_.load(std::memory_order_relaxed)
    };
    const AudioEQ::BiquadCoefficients mid{
        midB0_.load(std::memory_order_relaxed),
        midB1_.load(std::memory_order_relaxed),
        midB2_.load(std::memory_order_relaxed),
        midA1_.load(std::memory_order_relaxed),
        midA2_.load(std::memory_order_relaxed)
    };
    const AudioEQ::BiquadCoefficients high{
        highB0_.load(std::memory_order_relaxed),
        highB1_.load(std::memory_order_relaxed),
        highB2_.load(std::memory_order_relaxed),
        highA1_.load(std::memory_order_relaxed),
        highA2_.load(std::memory_order_relaxed)
    };

    for (size_t frameIndex = 0; frameIndex < frames; ++frameIndex) {
        for (size_t channelIndex = 0; channelIndex < channels; ++channelIndex) {
            const size_t sampleIndex = frameIndex * channels + channelIndex;
            auto& state = channelStates_[channelIndex];
            float sample = buffer[sampleIndex];
            sample = AudioEQ::processSample(low, sample, state.low.z1, state.low.z2);
            sample = AudioEQ::processSample(mid, sample, state.mid.z1, state.mid.z2);
            sample = AudioEQ::processSample(high, sample, state.high.z1, state.high.z2);
            buffer[sampleIndex] = sample;
        }
    }
}

void PlaybackEqualiser::updateCoefficients(const float sampleRate,
                                           const float low,
                                           const float mid,
                                           const float high) {
    if (sampleRate <= 0.0f) {
        return;
    }

    const auto cascade = AudioEQ::makeCascade(sampleRate, low, mid, high);

    lowB0_.store(cascade.low.b0, std::memory_order_relaxed);
    lowB1_.store(cascade.low.b1, std::memory_order_relaxed);
    lowB2_.store(cascade.low.b2, std::memory_order_relaxed);
    lowA1_.store(cascade.low.a1, std::memory_order_relaxed);
    lowA2_.store(cascade.low.a2, std::memory_order_relaxed);

    midB0_.store(cascade.mid.b0, std::memory_order_relaxed);
    midB1_.store(cascade.mid.b1, std::memory_order_relaxed);
    midB2_.store(cascade.mid.b2, std::memory_order_relaxed);
    midA1_.store(cascade.mid.a1, std::memory_order_relaxed);
    midA2_.store(cascade.mid.a2, std::memory_order_relaxed);

    highB0_.store(cascade.high.b0, std::memory_order_relaxed);
    highB1_.store(cascade.high.b1, std::memory_order_relaxed);
    highB2_.store(cascade.high.b2, std::memory_order_relaxed);
    highA1_.store(cascade.high.a1, std::memory_order_relaxed);
    highA2_.store(cascade.high.a2, std::memory_order_relaxed);
}

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
    const float clampedLow = std::isfinite(low) ? std::max(0.0f, low) : 1.0f;
    const float clampedMid = std::isfinite(mid) ? std::max(0.0f, mid) : 1.0f;
    const float clampedHigh = std::isfinite(high) ? std::max(0.0f, high) : 1.0f;
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
        std::fill_n(buffer, frames * channels, 0.0f);
        return;
    }
    // https://eel.is/c++draft/atomics.order
    // Accept only one complete coefficient version; the callback keeps its previous set while a writer is active.
    const auto version = coefficientVersion_.load();
    if ((version & 1u) == 0 && version != appliedVersion_) {
        const AudioEQ::CascadeCoefficients pending{
            {lowB0_.load(), lowB1_.load(), lowB2_.load(), lowA1_.load(), lowA2_.load()},
            {midB0_.load(), midB1_.load(), midB2_.load(), midA1_.load(), midA2_.load()},
            {highB0_.load(), highB1_.load(), highB2_.load(), highA1_.load(), highA2_.load()}};
        if (coefficientVersion_.load() == version) {
            targetCoefficients_ = pending;
            transitionRemaining_ = static_cast<std::size_t>(std::max(1.0f, sampleRate_.load() * .005f));
            appliedVersion_ = version;
        }
    }
    // https://www.w3.org/TR/audio-eq-cookbook/
    for (size_t frameIndex = 0; frameIndex < frames; ++frameIndex) {
        if (transitionRemaining_ > 0) {
            const auto advance = [this](AudioEQ::BiquadCoefficients& current, const AudioEQ::BiquadCoefficients& target) {
                const float fraction = 1.0f / static_cast<float>(transitionRemaining_);
                current.b0 += fraction * (target.b0 - current.b0);
                current.b1 += fraction * (target.b1 - current.b1);
                current.b2 += fraction * (target.b2 - current.b2);
                current.a1 += fraction * (target.a1 - current.a1);
                current.a2 += fraction * (target.a2 - current.a2);
            };
            advance(currentCoefficients_.low, targetCoefficients_.low);
            advance(currentCoefficients_.mid, targetCoefficients_.mid);
            advance(currentCoefficients_.high, targetCoefficients_.high);
            --transitionRemaining_;
        }
        for (size_t channelIndex = 0; channelIndex < channels; ++channelIndex) {
            const size_t sampleIndex = frameIndex * channels + channelIndex;
            auto& state = channelStates_[channelIndex];
            float sample = buffer[sampleIndex];
            sample = AudioEQ::processSample(currentCoefficients_.low, sample, state.low.z1, state.low.z2);
            sample = AudioEQ::processSample(currentCoefficients_.mid, sample, state.mid.z1, state.mid.z2);
            sample = AudioEQ::processSample(currentCoefficients_.high, sample, state.high.z1, state.high.z2);
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
    std::lock_guard lock(coefficientMutex_);
    coefficientVersion_.fetch_add(1);

    lowB0_.store(cascade.low.b0);
    lowB1_.store(cascade.low.b1);
    lowB2_.store(cascade.low.b2);
    lowA1_.store(cascade.low.a1);
    lowA2_.store(cascade.low.a2);

    midB0_.store(cascade.mid.b0);
    midB1_.store(cascade.mid.b1);
    midB2_.store(cascade.mid.b2);
    midA1_.store(cascade.mid.a1);
    midA2_.store(cascade.mid.a2);

    highB0_.store(cascade.high.b0);
    highB1_.store(cascade.high.b1);
    highB2_.store(cascade.high.b2);
    highA1_.store(cascade.high.a1);
    highA2_.store(cascade.high.a2);
    coefficientVersion_.fetch_add(1);
}

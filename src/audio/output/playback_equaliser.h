#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

class PlaybackEqualiser {
public:
    struct BiquadState {
        float z1 = 0.0f;
        float z2 = 0.0f;
    };

    void configure(float sampleRate, size_t channels);
    void setEnabled(bool enabled);
    void setGains(float low, float mid, float high);
    void requestReset();
    void processInterleaved(float* buffer, size_t frames, size_t channels);

private:
    struct ChannelState {
        BiquadState low;
        BiquadState mid;
        BiquadState high;
    };

    std::vector<ChannelState> channelStates_;
    std::atomic<bool> enabled_{false};
    std::atomic<bool> resetRequested_{false};
    std::atomic<float> sampleRate_{0.0f};
    std::atomic<float> lowGain_{1.0f};
    std::atomic<float> midGain_{1.0f};
    std::atomic<float> highGain_{1.0f};

    std::atomic<float> lowB0_{1.0f};
    std::atomic<float> lowB1_{0.0f};
    std::atomic<float> lowB2_{0.0f};
    std::atomic<float> lowA1_{0.0f};
    std::atomic<float> lowA2_{0.0f};

    std::atomic<float> midB0_{1.0f};
    std::atomic<float> midB1_{0.0f};
    std::atomic<float> midB2_{0.0f};
    std::atomic<float> midA1_{0.0f};
    std::atomic<float> midA2_{0.0f};

    std::atomic<float> highB0_{1.0f};
    std::atomic<float> highB1_{0.0f};
    std::atomic<float> highB2_{0.0f};
    std::atomic<float> highA1_{0.0f};
    std::atomic<float> highA2_{0.0f};

    void updateCoefficients(float sampleRate, float low, float mid, float high);
};

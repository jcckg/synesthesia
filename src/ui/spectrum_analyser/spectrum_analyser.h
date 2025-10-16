#pragma once

#include <imgui.h>
#include <implot.h>
#include <vector>
#include "audio_input.h"

class SpectrumAnalyser {
public:
    SpectrumAnalyser() = default;

    void drawSpectrumWindow(
        const std::vector<float>& smoothedMagnitudes,
        const std::vector<AudioInput::DeviceInfo>& devices,
        int selectedDeviceIndex,
        const ImVec2& displaySize,
        float sidebarWidth,
        bool sidebarOnLeft = false,
        float bottomPanelHeight = 0.0f
    );

    void resetTemporalBuffers();

private:
    static constexpr float SPECTRUM_HEIGHT = 180.0f;
    static constexpr int LINE_COUNT = 800;
    static constexpr int BASE_SMOOTHING_WINDOW_SIZE = 5;
    static constexpr float TEMPORAL_SMOOTHING_FACTOR = 0.62f;
    static constexpr float GAUSSIAN_SIGMA = 1.0f;
    static constexpr float CUBIC_TENSION = 0.5f;

    std::vector<float> previousFrameData;
    std::vector<float> smoothingBuffer1;
    std::vector<float> smoothingBuffer2;
    std::vector<float> gaussianWeights;
    std::vector<float> cachedFrequencies;
    float lastCachedSampleRate = 0.0f;
    bool buffersInitialised = false;

    static float getSampleRate(const std::vector<AudioInput::DeviceInfo>& devices, int selectedDeviceIndex);
    void prepareSpectrumData(std::vector<float>& xData, std::vector<float>& yData,
                            const std::vector<float>& magnitudes, float sampleRate);
    void smoothData(std::vector<float>& yData);
    void applyTemporalSmoothing(std::vector<float>& yData);
    void applyGaussianSmoothing(std::vector<float>& yData);
    void applyDynamicRangeCompensation(std::vector<float>& yData);
    static float cubicInterpolate(float y0, float y1, float y2, float y3, float t);
    static int getFrequencyDependentWindowSize(int index);
    float gaussianWeight(int distance, float sigma);
    void initialiseBuffers();
    void precomputeGaussianWeights();
    static float calculateLocalVariance(const std::vector<float>& yData, int centre, int windowSize);
};

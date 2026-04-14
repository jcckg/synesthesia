#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "colour/colour_core.h"

namespace PhaseAnalysis {
struct PhaseFeatureMetrics;
}

namespace SpectralPresentation {

struct Settings {
    float lowGain = 1.0f;
    float midGain = 1.0f;
    float highGain = 1.0f;
    ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020;
    bool applyGamutMapping = true;
};

struct Frame {
    std::vector<float> magnitudes;
    std::vector<float> phases;
    std::vector<float> frequencies;
    float sampleRate = 0.0f;
};

struct PreparedFrame {
    std::vector<float> visualiserMagnitudes;
    ColourCore::FrameResult colourResult;
};

Frame mixChannels(const std::vector<std::vector<float>>& magnitudes,
                  const std::vector<std::vector<float>>& phases,
                  const std::vector<std::vector<float>>& frequencies,
                  std::uint32_t channels,
                  float sampleRate);

std::vector<float> buildSharedMagnitudes(const Frame& frame,
                                         const Settings& settings);

std::vector<float> buildVisualiserMagnitudes(const std::vector<float>& sharedMagnitudes,
                                             float sampleRate,
                                             const ColourCore::FrameResult& colourResult);

PreparedFrame prepareFrame(const Frame& frame,
                           const Settings& settings,
                           float loudnessDb,
                           const PhaseAnalysis::PhaseFeatureMetrics& phaseMetrics);

PreparedFrame prepareFrame(const Frame& frame,
                           const Settings& settings,
                           float loudnessDb,
                           const Frame* previousFrame,
                           float deltaTimeSeconds);

PreparedFrame prepareFrame(const Frame& frame,
                           const Settings& settings,
                           float loudnessDb);

std::array<float, 3> displayRGBFromXYZ(float X,
                                       float Y,
                                       float Z,
                                       const Settings& settings);

ColourCore::FrameResult buildColourResult(const Frame& frame,
                                          const Settings& settings,
                                          float loudnessDb);

}

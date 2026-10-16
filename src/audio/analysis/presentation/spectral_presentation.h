#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "colour/colour_core.h"
#include "audio/analysis/fft/bass_analyser.h"

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
    std::shared_ptr<const AudioAnalysis::BassSpectrum> bass;
    std::vector<float> measurementMagnitudes;
    std::vector<float> measurementFrequencies;
    std::vector<float> measurementPowerWeights;
    std::size_t measurementFftSize = 0;
};

struct PreparedFrame {
    std::vector<float> analysisMagnitudes;
    ColourCore::FrameResult colourResult;
    std::shared_ptr<const ColourPresentation::SpectralContributions> contributions;
    float frequencyResolution = 0.0f;
};

Frame mixChannels(const std::vector<std::vector<float>>& magnitudes,
                  const std::vector<std::vector<float>>& phases,
                  const std::vector<std::vector<float>>& frequencies,
                  std::uint32_t channels,
                  float sampleRate);

std::vector<float> buildSharedMagnitudes(const Frame& frame,
                                         const Settings& settings);

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

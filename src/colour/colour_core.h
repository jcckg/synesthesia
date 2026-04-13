#pragma once

#include <array>
#include <limits>
#include <span>

#include "constants.h"

namespace PhaseAnalysis {
struct PhaseFeatureMetrics;
}

namespace ColourCore {

using ColourSpace = ::ColourSpace;

constexpr float LOUDNESS_DB_UNSPECIFIED = std::numeric_limits<float>::quiet_NaN();

struct WhitePoint {
    float X;
    float Y;
    float Z;
};

struct RGB {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct XYZ {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

struct Lab {
    float L = 0.0f;
    float a = 0.0f;
    float b = 0.0f;
};

struct SpectralCharacteristics {
    float flatness = 0.5f;
    float centroid = 0.0f;
    float spread = 0.0f;
    float normalisedSpread = 0.0f;
};

struct OutputSettings {
    ColourSpace colourSpace = ColourSpace::Rec2020;
    bool applyGamutMapping = true;
};

struct FrameResult {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float dominantWavelength = 0.0f;
    float dominantFrequency = 0.0f;
    float colourIntensity = 0.0f;
    float L = 0.0f;
    float a = 0.0f;
    float b_comp = 0.0f;
    float spectralCentroid = 0.0f;
    float spectralFlatness = 0.0f;
    float spectralSpread = 0.0f;
    float spectralRolloff = 0.0f;
    float spectralCrestFactor = 0.0f;
    float loudnessDb = 0.0f;
    float frameLoudnessDb = 0.0f;
    float brightnessLoudnessDb = 0.0f;
    float loudnessNormalised = 0.0f;
    float brightnessNormalised = 0.0f;
    float transientMix = 0.0f;
    float estimatedSPL = 0.0f;
    float luminanceCdM2 = 0.0f;
    float phaseInstabilityNorm = 0.0f;
    float phaseCoherenceNorm = 0.0f;
    float phaseTransientNorm = 0.0f;
};

struct VideoProfile {
    const char* filter = "";
    const char* colourSpace = "";
    const char* primaries = "";
    const char* transfer = "";
    const char* pixelFormat = "";
};

struct PngProfile {
    bool useSrgbChunk = false;
    unsigned renderingIntent = 0;
    bool useCicpChunk = false;
    unsigned colourPrimaries = 0;
    unsigned transferCharacteristics = 0;
    unsigned matrixCoefficients = 0;
    unsigned fullRangeFlag = 1;
};

WhitePoint D50White();
WhitePoint D65White();

void interpolateCIE(float wavelength, float& X, float& Y, float& Z);
float logFrequencyToWavelength(float frequency);
float wavelengthToLogFrequency(float wavelength);
float chromaticDominantWavelength(float X, float Y, float Z);

void XYZtoLab(float X, float Y, float Z, float& L, float& a, float& bValue);
void LabtoXYZ(float L, float a, float bValue, float& X, float& Y, float& Z);

void XYZtoOklab(float X, float Y, float Z, float& L, float& a, float& bValue);
void OklabtoXYZ(float L, float a, float bValue, float& X, float& Y, float& Z);

void RGBtoXYZ(float r, float g, float b, float& X, float& Y, float& Z, ColourSpace colourSpace);
void XYZtoRGB(float X, float Y, float Z, float& r, float& g, float& b,
              ColourSpace colourSpace,
              bool applyGamma = true,
              bool applyGamutMapping = true);

void RGBtoLab(float r, float g, float b, float& L, float& a, float& bValue,
              ColourSpace colourSpace);
void LabtoRGB(float L, float a, float bValue, float& r, float& g, float& bOut,
              ColourSpace colourSpace,
              bool applyGamutMapping = true);

RGB projectToRGB(const XYZ& xyz, const OutputSettings& settings);
const VideoProfile& videoProfileFor(ColourSpace colourSpace);
const PngProfile& pngProfileFor(ColourSpace colourSpace);
SpectralCharacteristics calculateSpectralCharacteristics(std::span<const float> spectrum, float sampleRate);

FrameResult analyseSpectrum(std::span<const float> magnitudes,
                            std::span<const float> phases,
                            std::span<const float> frequencies,
                            float sampleRate,
                            const OutputSettings& outputSettings,
                            float overrideLoudnessDb = LOUDNESS_DB_UNSPECIFIED,
                            const PhaseAnalysis::PhaseFeatureMetrics* phaseMetrics = nullptr);

}

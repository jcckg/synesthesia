#include "audio/analysis/presentation/phase_colour.h"

#include <algorithm>
#include <cmath>

namespace SpectralPresentation {

namespace {

std::array<float, 3> encodeDisplayRGB(const float X,
                                      const float Y,
                                      const float Z,
                                      const Settings& settings) {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    ColourMapper::XYZtoRGB(X, Y, Z, r, g, b, settings.colourSpace, true, settings.applyGamutMapping);

    return {r, g, b};
}

} // namespace

void applyPhaseColourInfluence(ColourMapper::ColourResult& colourResult,
                               const PhaseAnalysis::PhaseFeatureMetrics& phaseMetrics,
                               const Settings& settings) {
    colourResult.phaseInstabilityNorm = phaseMetrics.instabilityNorm;
    colourResult.phaseCoherenceNorm = phaseMetrics.coherenceNorm;
    colourResult.phaseTransientNorm = phaseMetrics.transientNorm;

    if (phaseMetrics.isNeutral()) {
        return;
    }

    float oklabL = 0.0f;
    float oklabA = 0.0f;
    float oklabB = 0.0f;
    ColourMapper::XYZtoOklab(colourResult.X, colourResult.Y, colourResult.Z, oklabL, oklabA, oklabB);

    const float coherence = std::clamp(phaseMetrics.coherenceNorm, 0.0f, 1.0f);
    const float instability = std::clamp(phaseMetrics.instabilityNorm, 0.0f, 1.0f);
    const float transient = std::clamp(phaseMetrics.transientNorm, 0.0f, 1.0f);
    const float stableWarmth = coherence * (1.0f - instability);
    const float unstableCoolness = instability * (1.0f - 0.3f * coherence);
    const float chromaScale = std::clamp(1.0f + 0.06f * stableWarmth - 0.05f * unstableCoolness, 0.92f, 1.08f);

    oklabA = oklabA * chromaScale + stableWarmth * 1.0f - unstableCoolness * 0.6f;
    oklabB = oklabB * chromaScale + stableWarmth * 0.6f - unstableCoolness * 1.4f;
    oklabL = std::clamp(
        oklabL + transient * (1.5f + 2.5f * std::clamp(colourResult.brightnessNormalised, 0.0f, 1.0f)) - unstableCoolness * 0.6f,
        0.0f,
        100.0f);

    ColourMapper::OklabtoXYZ(oklabL, oklabA, oklabB, colourResult.X, colourResult.Y, colourResult.Z);
    ColourMapper::XYZtoLab(colourResult.X, colourResult.Y, colourResult.Z, colourResult.L, colourResult.a, colourResult.b_comp);

    const auto rgb = encodeDisplayRGB(colourResult.X, colourResult.Y, colourResult.Z, settings);
    colourResult.r = rgb[0];
    colourResult.g = rgb[1];
    colourResult.b = rgb[2];
    colourResult.luminanceCdM2 =
        std::max(0.0f, colourResult.Y) * synesthesia::constants::REFERENCE_WHITE_LUMINANCE_CDM2;
}

} // namespace SpectralPresentation

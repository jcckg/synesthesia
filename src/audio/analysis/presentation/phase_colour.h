#pragma once

#include "audio/analysis/phase/phase_features.h"
#include "audio/analysis/presentation/spectral_presentation.h"

namespace SpectralPresentation {

void applyPhaseColourInfluence(ColourMapper::ColourResult& colourResult,
                               const PhaseAnalysis::PhaseFeatureMetrics& phaseMetrics,
                               const Settings& settings);

} // namespace SpectralPresentation

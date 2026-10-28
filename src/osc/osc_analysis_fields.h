#pragma once

#include <array>
#include "osc_addresses.h"
#include "osc_messages.h"

namespace Synesthesia::OSC {

template <typename Data>
struct OSCFloatField {
    const char* legacyAddress;
    const char* sourceAddress;
    float Data::* value;
};

inline constexpr std::array signalFields{
    OSCFloatField<OSCFrameSignalData>{kFrameSignalDominantFrequencyAddress, "/synesthesia/frame/analysis/source/dominant_frequency_hz", &OSCFrameSignalData::dominantFrequencyHz},
    OSCFloatField<OSCFrameSignalData>{kFrameSignalDominantWavelengthAddress, "/synesthesia/frame/analysis/source/dominant_wavelength_nm", &OSCFrameSignalData::dominantWavelengthNm},
    OSCFloatField<OSCFrameSignalData>{kFrameSignalDominantMagnitudeAddress, "/synesthesia/frame/analysis/source/dominant_magnitude", &OSCFrameSignalData::dominantMagnitude},
    OSCFloatField<OSCFrameSignalData>{kFrameSignalPhaseRadiansAddress, "/synesthesia/frame/analysis/source/interpolated_phase_radians", &OSCFrameSignalData::phaseRadians}
};

inline constexpr std::array spectralFields{
    OSCFloatField<OSCFrameSpectralData>{kFrameAnalysisSpectralFlatnessAddress, "/synesthesia/frame/analysis/source/spectral_flatness", &OSCFrameSpectralData::flatness},
    OSCFloatField<OSCFrameSpectralData>{kFrameAnalysisSpectralCentroidAddress, "/synesthesia/frame/analysis/source/spectral_centroid_hz", &OSCFrameSpectralData::centroidHz},
    OSCFloatField<OSCFrameSpectralData>{kFrameAnalysisSpectralSpreadAddress, "/synesthesia/frame/analysis/source/spectral_spread_hz", &OSCFrameSpectralData::spreadHz},
    OSCFloatField<OSCFrameSpectralData>{kFrameAnalysisSpectralSpreadNormalisedAddress, "/synesthesia/frame/analysis/source/spectral_spread_normalised", &OSCFrameSpectralData::normalisedSpread},
    OSCFloatField<OSCFrameSpectralData>{kFrameAnalysisSpectralRelativeSpreadAddress, "/synesthesia/frame/analysis/source/spectral_relative_spread_normalised", &OSCFrameSpectralData::relativeSpreadNormalised},
    OSCFloatField<OSCFrameSpectralData>{kFrameAnalysisSpectralRolloffNormalisedAddress, "/synesthesia/frame/analysis/source/spectral_rolloff_normalised", &OSCFrameSpectralData::rolloffNormalised},
    OSCFloatField<OSCFrameSpectralData>{kFrameAnalysisSpectralCrestNormalisedAddress, "/synesthesia/frame/analysis/source/spectral_crest_normalised", &OSCFrameSpectralData::crestNormalised},
    OSCFloatField<OSCFrameSpectralData>{kFrameAnalysisSpectralRolloffAddress, "/synesthesia/frame/analysis/source/spectral_rolloff_hz", &OSCFrameSpectralData::rolloffHz},
    OSCFloatField<OSCFrameSpectralData>{kFrameAnalysisSpectralCrestAddress, "/synesthesia/frame/analysis/source/spectral_crest_factor", &OSCFrameSpectralData::crestFactor}
};

inline constexpr std::array loudnessFields{
    OSCFloatField<OSCFrameLoudnessData>{kFrameAnalysisLoudnessNormalisedAddress, "/synesthesia/frame/analysis/source/loudness_normalised", &OSCFrameLoudnessData::loudnessNormalised},
    OSCFloatField<OSCFrameLoudnessData>{kFrameAnalysisFrameLoudnessDbAddress, "/synesthesia/frame/analysis/source/frame_loudness_db", &OSCFrameLoudnessData::frameLoudnessDb},
    OSCFloatField<OSCFrameLoudnessData>{kFrameAnalysisMomentaryLoudnessAddress, "/synesthesia/frame/analysis/source/momentary_loudness_lufs", &OSCFrameLoudnessData::momentaryLoudnessLUFS},
    OSCFloatField<OSCFrameLoudnessData>{kFrameAnalysisBrightnessNormalisedAddress, "/synesthesia/frame/analysis/source/brightness_normalised", &OSCFrameLoudnessData::brightnessNormalised}
};

inline constexpr std::array phaseFields{
    OSCFloatField<OSCFramePhaseData>{kFrameAnalysisPhaseInstabilityAddress, "/synesthesia/frame/analysis/source/phase_instability_normalised", &OSCFramePhaseData::instabilityNorm},
    OSCFloatField<OSCFramePhaseData>{kFrameAnalysisPhaseCoherenceAddress, "/synesthesia/frame/analysis/source/phase_coherence_normalised", &OSCFramePhaseData::coherenceNorm},
    OSCFloatField<OSCFramePhaseData>{kFrameAnalysisPhaseTransientAddress, "/synesthesia/frame/analysis/source/phase_transient_normalised", &OSCFramePhaseData::transientNorm}
};

struct OSCColourAnalysisField {
    const char* sourceAddress;
    const char* presentedAddress;
    float OSCFrameColourAnalysis::* value;
};

inline constexpr std::array colourAnalysisFields{
    OSCColourAnalysisField{"/synesthesia/frame/analysis/source/luminance_y", "/synesthesia/frame/analysis/presented/luminance_y", &OSCFrameColourAnalysis::luminanceY},
    OSCColourAnalysisField{"/synesthesia/frame/analysis/source/chromaticity_x", "/synesthesia/frame/analysis/presented/chromaticity_x", &OSCFrameColourAnalysis::chromaticityX},
    OSCColourAnalysisField{"/synesthesia/frame/analysis/source/chromaticity_y", "/synesthesia/frame/analysis/presented/chromaticity_y", &OSCFrameColourAnalysis::chromaticityY},
    OSCColourAnalysisField{"/synesthesia/frame/analysis/source/oklab_lightness", "/synesthesia/frame/analysis/presented/oklab_lightness", &OSCFrameColourAnalysis::lightness},
    OSCColourAnalysisField{"/synesthesia/frame/analysis/source/oklab_chroma", "/synesthesia/frame/analysis/presented/oklab_chroma", &OSCFrameColourAnalysis::chroma},
    OSCColourAnalysisField{"/synesthesia/frame/analysis/source/oklab_hue_degrees", "/synesthesia/frame/analysis/presented/oklab_hue_degrees", &OSCFrameColourAnalysis::hueDegrees},
    OSCColourAnalysisField{"/synesthesia/frame/analysis/source/oklab_change_per_second", "/synesthesia/frame/analysis/presented/oklab_change_per_second", &OSCFrameColourAnalysis::colourChangePerSecond}
};

}

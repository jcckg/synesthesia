#pragma once

namespace Synesthesia::OSC {

inline constexpr const char* kFrameStatusAddress = "/synesthesia/frame/status";
inline constexpr const char* kFrameSmoothingEnabledAddress = "/synesthesia/frame/smoothing/enabled";
inline constexpr const char* kFrameSmoothingModeAddress = "/synesthesia/frame/smoothing/mode";
inline constexpr const char* kFrameSmoothingAmountAddress = "/synesthesia/frame/smoothing/amount";
inline constexpr const char* kFrameSmoothingManualResponseAddress = "/synesthesia/frame/smoothing/manual_response_seconds";
inline constexpr const char* kFrameSmoothingResponseAddress = "/synesthesia/frame/smoothing/response_seconds";
inline constexpr const char* kFrameSmoothingMinimumResponseAddress = "/synesthesia/frame/smoothing/minimum_response_seconds";
inline constexpr const char* kFrameSmoothingMaximumResponseAddress = "/synesthesia/frame/smoothing/maximum_response_seconds";
inline constexpr const char* kFrameSourceAnalysisAvailableAddress = "/synesthesia/frame/analysis/source/available";
inline constexpr const char* kFramePresentedAnalysisAvailableAddress = "/synesthesia/frame/analysis/presented/available";

inline constexpr const char* kFrameMetaSampleRateAddress = "/synesthesia/frame/meta/sample_rate";
inline constexpr const char* kFrameMetaFftSizeAddress = "/synesthesia/frame/meta/fft_size";
inline constexpr const char* kFrameMetaTimestampAddress = "/synesthesia/frame/meta/frame_timestamp_micros";

inline constexpr const char* kFrameSignalDominantFrequencyAddress = "/synesthesia/frame/signal/dominant_frequency_hz";
inline constexpr const char* kFrameSignalDominantWavelengthAddress = "/synesthesia/frame/signal/dominant_wavelength_nm";
inline constexpr const char* kFrameSignalDominantMagnitudeAddress = "/synesthesia/frame/signal/dominant_magnitude";
inline constexpr const char* kFrameSignalPhaseRadiansAddress = "/synesthesia/frame/signal/interpolated_phase_radians";

inline constexpr const char* kFrameColourDisplayRAddress = "/synesthesia/frame/colour/display_r";
inline constexpr const char* kFrameColourDisplayGAddress = "/synesthesia/frame/colour/display_g";
inline constexpr const char* kFrameColourDisplayBAddress = "/synesthesia/frame/colour/display_b";
inline constexpr const char* kFrameColourCIEXAddress = "/synesthesia/frame/colour/cie_x";
inline constexpr const char* kFrameMeasurementCIE2015XAddress = "/synesthesia/frame/measurement/cie2015_x";
inline constexpr const char* kFrameMeasurementCIE2015YAddress = "/synesthesia/frame/measurement/cie2015_y";
inline constexpr const char* kFrameMeasurementCIE2015ZAddress = "/synesthesia/frame/measurement/cie2015_z";
inline constexpr const char* kFrameMeasurementAvailableAddress = "/synesthesia/frame/measurement/available";
inline constexpr const char* kFrameColourCIEYAddress = "/synesthesia/frame/colour/cie_y";
inline constexpr const char* kFrameColourCIEZAddress = "/synesthesia/frame/colour/cie_z";
inline constexpr const char* kFrameColourOklabLAddress = "/synesthesia/frame/colour/oklab_l";
inline constexpr const char* kFrameColourOklabAAddress = "/synesthesia/frame/colour/oklab_a";
inline constexpr const char* kFrameColourOklabBAddress = "/synesthesia/frame/colour/oklab_b";

inline constexpr const char* kFrameAnalysisSpectralFlatnessAddress = "/synesthesia/frame/analysis/spectral_flatness";
inline constexpr const char* kFrameAnalysisSpectralCentroidAddress = "/synesthesia/frame/analysis/spectral_centroid_hz";
inline constexpr const char* kFrameAnalysisSpectralSpreadAddress = "/synesthesia/frame/analysis/spectral_spread_hz";
inline constexpr const char* kFrameAnalysisSpectralSpreadNormalisedAddress = "/synesthesia/frame/analysis/spectral_spread_normalised";
inline constexpr const char* kFrameAnalysisSpectralRelativeSpreadAddress = "/synesthesia/frame/analysis/spectral_relative_spread_normalised";
inline constexpr const char* kFrameAnalysisSpectralRolloffNormalisedAddress = "/synesthesia/frame/analysis/spectral_rolloff_normalised";
inline constexpr const char* kFrameAnalysisSpectralCrestNormalisedAddress = "/synesthesia/frame/analysis/spectral_crest_normalised";
inline constexpr const char* kFrameAnalysisSpectralRolloffAddress = "/synesthesia/frame/analysis/spectral_rolloff_hz";
inline constexpr const char* kFrameAnalysisSpectralCrestAddress = "/synesthesia/frame/analysis/spectral_crest_factor";
inline constexpr const char* kFrameAnalysisSpectralFluxAddress = "/synesthesia/frame/analysis/spectral_flux";

inline constexpr const char* kFrameAnalysisLoudnessNormalisedAddress = "/synesthesia/frame/analysis/loudness_normalised";
inline constexpr const char* kFrameAnalysisFrameLoudnessDbAddress = "/synesthesia/frame/analysis/frame_loudness_db";
inline constexpr const char* kFrameAnalysisMomentaryLoudnessAddress = "/synesthesia/frame/analysis/momentary_loudness_lufs";
inline constexpr const char* kFrameAnalysisBrightnessNormalisedAddress = "/synesthesia/frame/analysis/brightness_normalised";

inline constexpr const char* kFrameAnalysisOnsetDetectedAddress = "/synesthesia/frame/analysis/onset_detected";

inline constexpr const char* kFrameAnalysisPhaseInstabilityAddress = "/synesthesia/frame/analysis/phase_instability_normalised";
inline constexpr const char* kFrameAnalysisPhaseCoherenceAddress = "/synesthesia/frame/analysis/phase_coherence_normalised";
inline constexpr const char* kFrameAnalysisPhaseTransientAddress = "/synesthesia/frame/analysis/phase_transient_normalised";

inline constexpr const char* kControlSmoothingAddress = "/synesthesia/control/smoothing";
inline constexpr const char* kControlColourSpaceAddress = "/synesthesia/control/colour_space";
inline constexpr const char* kControlGamutMappingAddress = "/synesthesia/control/gamut_mapping";

}

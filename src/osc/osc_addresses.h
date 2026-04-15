#pragma once

namespace Synesthesia::OSC {

inline constexpr const char* kFrameMetaSampleRateAddress = "/synesthesia/frame/meta/sample_rate";
inline constexpr const char* kFrameMetaFftSizeAddress = "/synesthesia/frame/meta/fft_size";
inline constexpr const char* kFrameMetaTimestampAddress = "/synesthesia/frame/meta/frame_timestamp_micros";

inline constexpr const char* kFrameSignalDominantFrequencyAddress = "/synesthesia/frame/signal/dominant_frequency_hz";
inline constexpr const char* kFrameSignalDominantWavelengthAddress = "/synesthesia/frame/signal/dominant_wavelength_nm";
inline constexpr const char* kFrameSignalVisualiserMagnitudeAddress = "/synesthesia/frame/signal/visualiser_magnitude";
inline constexpr const char* kFrameSignalPhaseRadiansAddress = "/synesthesia/frame/signal/interpolated_phase_radians";

inline constexpr const char* kFrameColourDisplayRAddress = "/synesthesia/frame/colour/display_r";
inline constexpr const char* kFrameColourDisplayGAddress = "/synesthesia/frame/colour/display_g";
inline constexpr const char* kFrameColourDisplayBAddress = "/synesthesia/frame/colour/display_b";
inline constexpr const char* kFrameColourCIEXAddress = "/synesthesia/frame/colour/cie_x";
inline constexpr const char* kFrameColourCIEYAddress = "/synesthesia/frame/colour/cie_y";
inline constexpr const char* kFrameColourCIEZAddress = "/synesthesia/frame/colour/cie_z";
inline constexpr const char* kFrameColourOklabLAddress = "/synesthesia/frame/colour/oklab_l";
inline constexpr const char* kFrameColourOklabAAddress = "/synesthesia/frame/colour/oklab_a";
inline constexpr const char* kFrameColourOklabBAddress = "/synesthesia/frame/colour/oklab_b";

inline constexpr const char* kFrameAnalysisSpectralFlatnessAddress = "/synesthesia/frame/analysis/spectral_flatness";
inline constexpr const char* kFrameAnalysisSpectralCentroidAddress = "/synesthesia/frame/analysis/spectral_centroid_hz";
inline constexpr const char* kFrameAnalysisSpectralSpreadAddress = "/synesthesia/frame/analysis/spectral_spread_hz";
inline constexpr const char* kFrameAnalysisSpectralSpreadNormalisedAddress = "/synesthesia/frame/analysis/spectral_spread_normalised";
inline constexpr const char* kFrameAnalysisSpectralRolloffAddress = "/synesthesia/frame/analysis/spectral_rolloff_hz";
inline constexpr const char* kFrameAnalysisSpectralCrestAddress = "/synesthesia/frame/analysis/spectral_crest_factor";
inline constexpr const char* kFrameAnalysisSpectralFluxAddress = "/synesthesia/frame/analysis/spectral_flux";

inline constexpr const char* kFrameAnalysisLoudnessDbAddress = "/synesthesia/frame/analysis/loudness_db";
inline constexpr const char* kFrameAnalysisLoudnessNormalisedAddress = "/synesthesia/frame/analysis/loudness_normalised";
inline constexpr const char* kFrameAnalysisFrameLoudnessDbAddress = "/synesthesia/frame/analysis/frame_loudness_db";
inline constexpr const char* kFrameAnalysisMomentaryLoudnessAddress = "/synesthesia/frame/analysis/momentary_loudness_lufs";
inline constexpr const char* kFrameAnalysisEstimatedSPLAddress = "/synesthesia/frame/analysis/estimated_spl";
inline constexpr const char* kFrameAnalysisLuminanceAddress = "/synesthesia/frame/analysis/luminance_cd_m2";
inline constexpr const char* kFrameAnalysisBrightnessNormalisedAddress = "/synesthesia/frame/analysis/brightness_normalised";

inline constexpr const char* kFrameAnalysisTransientMixAddress = "/synesthesia/frame/analysis/transient_mix";
inline constexpr const char* kFrameAnalysisOnsetDetectedAddress = "/synesthesia/frame/analysis/onset_detected";

inline constexpr const char* kFrameAnalysisPhaseInstabilityAddress = "/synesthesia/frame/analysis/phase_instability_normalised";
inline constexpr const char* kFrameAnalysisPhaseCoherenceAddress = "/synesthesia/frame/analysis/phase_coherence_normalised";
inline constexpr const char* kFrameAnalysisPhaseTransientAddress = "/synesthesia/frame/analysis/phase_transient_normalised";

inline constexpr const char* kFrameSmoothingOnsetDetectedAddress = "/synesthesia/frame/smoothing/onset_detected";
inline constexpr const char* kFrameSmoothingSpectralFluxAddress = "/synesthesia/frame/smoothing/spectral_flux";
inline constexpr const char* kFrameSmoothingSpectralFlatnessAddress = "/synesthesia/frame/smoothing/spectral_flatness";
inline constexpr const char* kFrameSmoothingLoudnessNormalisedAddress = "/synesthesia/frame/smoothing/loudness_normalised";
inline constexpr const char* kFrameSmoothingBrightnessNormalisedAddress = "/synesthesia/frame/smoothing/brightness_normalised";
inline constexpr const char* kFrameSmoothingSpectralSpreadAddress = "/synesthesia/frame/smoothing/spectral_spread_normalised";
inline constexpr const char* kFrameSmoothingSpectralRolloffAddress = "/synesthesia/frame/smoothing/spectral_rolloff_normalised";
inline constexpr const char* kFrameSmoothingSpectralCrestAddress = "/synesthesia/frame/smoothing/spectral_crest_normalised";
inline constexpr const char* kFrameSmoothingPhaseInstabilityAddress = "/synesthesia/frame/smoothing/phase_instability_normalised";
inline constexpr const char* kFrameSmoothingPhaseCoherenceAddress = "/synesthesia/frame/smoothing/phase_coherence_normalised";
inline constexpr const char* kFrameSmoothingPhaseTransientAddress = "/synesthesia/frame/smoothing/phase_transient_normalised";

inline constexpr const char* kControlSmoothingAddress = "/synesthesia/control/smoothing";
inline constexpr const char* kControlSpectrumSmoothingAddress = "/synesthesia/control/spectrum_smoothing";
inline constexpr const char* kControlColourSpaceAddress = "/synesthesia/control/colour_space";
inline constexpr const char* kControlGamutMappingAddress = "/synesthesia/control/gamut_mapping";

}

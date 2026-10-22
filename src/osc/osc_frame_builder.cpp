#include "osc_frame_builder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>

#include "ui/smoothing/smoothing_features.h"

namespace Synesthesia::OSC {

namespace {

struct SpectralPosition {
    std::size_t lower;
    std::size_t upper;
    float fraction;
};

std::optional<SpectralPosition> spectralPosition(const OSCFrameUpdate& update) {
    const auto count = update.magnitudes.size();
    const float frequency = update.colourResult.dominantFrequency;
    if (count < 2 || !std::isfinite(frequency) || frequency < 0.0f ||
        !std::isfinite(update.sampleRate) || update.sampleRate <= 0.0f) return std::nullopt;
    if (update.frequencies.empty()) {
        const float position = frequency / update.sampleRate * (2.0f * static_cast<float>(count - 1));
        if (!std::isfinite(position) || position > static_cast<float>(count - 1)) return std::nullopt;
        const auto lower = static_cast<std::size_t>(position);
        return SpectralPosition{lower, std::min(lower + 1, count - 1), position - static_cast<float>(lower)};
    }
    if (update.frequencies.size() != count) return std::nullopt;
    std::optional<std::size_t> lower, upper;
    for (std::size_t i = 0; i < count; ++i) {
        const float candidate = update.frequencies[i];
        if (!std::isfinite(candidate) || candidate < 0.0f || candidate > update.sampleRate * 0.5f) continue;
        if (candidate <= frequency && (!lower || candidate > update.frequencies[*lower] ||
            (candidate == update.frequencies[*lower] && update.magnitudes[i] > update.magnitudes[*lower]))) lower = i;
        if (candidate >= frequency && (!upper || candidate < update.frequencies[*upper] ||
            (candidate == update.frequencies[*upper] && update.magnitudes[i] > update.magnitudes[*upper]))) upper = i;
    }
    if (!lower || !upper) return std::nullopt;
    const float width = update.frequencies[*upper] - update.frequencies[*lower];
    return SpectralPosition{*lower, *upper, width > 0.0f ? (frequency - update.frequencies[*lower]) / width : 0.0f};
}

float interpolateMagnitude(const std::span<const float> magnitudes,
                           const std::optional<SpectralPosition>& position) {
    if (!position) return std::numeric_limits<float>::quiet_NaN();
    const float lower = magnitudes[position->lower], upper = magnitudes[position->upper];
    if (!std::isfinite(lower) || !std::isfinite(upper)) return std::numeric_limits<float>::quiet_NaN();
    return (1.0f - position->fraction) * lower + position->fraction * upper;
}

float interpolatePhase(const OSCFrameUpdate& update,
                       const std::optional<SpectralPosition>& position) {
    if (!position || update.phases.size() != update.magnitudes.size())
        return std::numeric_limits<float>::quiet_NaN();
    const float lower = update.phases[position->lower], upper = update.phases[position->upper];
    if (!std::isfinite(lower) || !std::isfinite(upper)) return std::numeric_limits<float>::quiet_NaN();
    constexpr double twoPi = 2.0 * std::numbers::pi;
    const double difference = std::remainder(static_cast<double>(upper) - lower, twoPi);
    return static_cast<float>(std::remainder(lower + position->fraction * difference, twoPi));
}

int64_t currentTimestampMicros() {
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    return static_cast<int64_t>(std::max(now, static_cast<decltype(now)>(0)));
}

int32_t resolveFftSize(const std::span<const float> magnitudes) {
    return magnitudes.size() > 1
        ? static_cast<int32_t>((magnitudes.size() - 1) * 2)
        : 0;
}

}

OSCFrameData buildFrameData(const OSCFrameUpdate& update) {
    OSCFrameData frame{};
    const auto features = UI::Smoothing::buildSignalFeatures(update.colourResult);
    frame.meta.sampleRate = std::isfinite(update.sampleRate) && update.sampleRate > 0.0f &&
        static_cast<double>(update.sampleRate) <= std::numeric_limits<int32_t>::max()
        ? static_cast<int32_t>(update.sampleRate) : 0;
    frame.meta.fftSize = update.fftSize > 0 ? update.fftSize : resolveFftSize(update.magnitudes);
    const double timestamp = update.sourceTimestamp * 1e6;
    frame.meta.frameTimestamp = std::isfinite(timestamp) &&
        timestamp >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
        timestamp < static_cast<double>(std::numeric_limits<int64_t>::max())
        ? static_cast<int64_t>(timestamp) : currentTimestampMicros();

    frame.signal.dominantFrequencyHz = update.colourResult.dominantFrequency;
    frame.signal.dominantWavelengthNm = update.colourResult.dominantWavelength;
    const auto position = spectralPosition(update);
    frame.signal.dominantMagnitude = interpolateMagnitude(update.magnitudes, position);
    frame.signal.phaseRadians = interpolatePhase(update, position);

    frame.colour.displayR = update.displayColour.r;
    frame.colour.displayG = update.displayColour.g;
    frame.colour.displayB = update.displayColour.b;
    frame.colour.cieX = std::isfinite(update.presentedXYZ.X) ? update.presentedXYZ.X : update.colourResult.X;
    frame.colour.cieY = std::isfinite(update.presentedXYZ.Y) ? update.presentedXYZ.Y : update.colourResult.Y;
    frame.colour.cieZ = std::isfinite(update.presentedXYZ.Z) ? update.presentedXYZ.Z : update.colourResult.Z;
    ColourCore::XYZtoOklab(frame.colour.cieX, frame.colour.cieY, frame.colour.cieZ,
        frame.colour.oklabL, frame.colour.oklabA, frame.colour.oklabB);
    frame.colour.measurementXYZ = update.colourResult.spectral.xyz;
    frame.colour.measurementAvailable = update.colourResult.spectral.available;

    frame.spectral.flatness = update.colourResult.spectralFlatness;
    frame.spectral.centroidHz = update.colourResult.spectralCentroid;
    frame.spectral.spreadHz = update.colourResult.spectralSpread;
    frame.spectral.normalisedSpread = std::clamp(update.colourResult.spectralSpread / 5000.0f, 0.0f, 1.0f);
    frame.spectral.relativeSpreadNormalised = features.spectralSpreadNorm;
    frame.spectral.rolloffNormalised = features.spectralRolloffNorm;
    frame.spectral.crestNormalised = features.spectralCrestNorm;
    frame.spectral.rolloffHz = update.colourResult.spectralRolloff;
    frame.spectral.crestFactor = update.colourResult.spectralCrestFactor;
    frame.spectral.spectralFlux = update.analysisSignals.spectralFlux;

    frame.loudness.loudnessNormalised = update.colourResult.loudnessNormalised;
    frame.loudness.frameLoudnessDb = update.colourResult.frameLoudnessDb;
    frame.loudness.momentaryLoudnessLUFS = update.colourResult.loudnessIsLufs
        ? update.colourResult.loudnessDb : ColourCore::LOUDNESS_DB_UNSPECIFIED;
    frame.loudness.brightnessNormalised = update.colourResult.brightnessNormalised;

    frame.transient.onsetDetected = update.analysisSignals.onsetDetected;

    frame.phase.instabilityNorm = update.colourResult.phaseInstabilityNorm;
    frame.phase.coherenceNorm = update.colourResult.phaseCoherenceNorm;
    frame.phase.transientNorm = update.colourResult.phaseTransientNorm;

    return frame;
}

}

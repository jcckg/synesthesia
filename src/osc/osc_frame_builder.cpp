#include "osc_frame_builder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>

#include "ui/smoothing/smoothing_features.h"
#include "ui/smoothing/smoothing.h"

namespace Synesthesia::OSC {

namespace {

bool validXYZ(const ColourCore::XYZ& xyz) {
    return std::isfinite(xyz.X) && std::isfinite(xyz.Y) && std::isfinite(xyz.Z) && xyz.Y >= 0.0f;
}

OSCFrameColourAnalysis analyseColour(const ColourCore::XYZ& xyz, bool available) {
    OSCFrameColourAnalysis result;
    if (!available || !validXYZ(xyz)) return result;
    result.available = true;
    result.luminanceY = xyz.Y;
    const float sum = xyz.X + xyz.Y + xyz.Z;
    if (sum > 1e-12f) {
        result.chromaticityX = xyz.X / sum;
        result.chromaticityY = xyz.Y / sum;
    }
    float a, b;
    ColourCore::XYZtoOklab(xyz.X, xyz.Y, xyz.Z, result.lightness, a, b);
    result.chroma = std::hypot(a, b);
    if (result.chroma > 1e-5f) {
        const float hue = std::atan2(b, a) * 180.0f / std::numbers::pi_v<float>;
        result.hueDegrees = hue < 0.0f ? hue + 360.0f : hue;
    }
    return result;
}

float colourChange(const ColourCore::XYZ& previous, const ColourCore::XYZ& current, double dt) {
    if (!validXYZ(previous) || !validXYZ(current) || !std::isfinite(dt) || dt < 0.0) return NAN;
    const bool same = previous.X == current.X && previous.Y == current.Y && previous.Z == current.Z;
    if (dt == 0.0) return same ? 0.0f : NAN;
    float previousL, previousA, previousB, currentL, currentA, currentB;
    ColourCore::XYZtoOklab(previous.X, previous.Y, previous.Z, previousL, previousA, previousB);
    ColourCore::XYZtoOklab(current.X, current.Y, current.Z, currentL, currentA, currentB);
    const double change = std::hypot(static_cast<double>(currentL) - static_cast<double>(previousL),
        static_cast<double>(currentA) - static_cast<double>(previousA), static_cast<double>(currentB) - static_cast<double>(previousB)) / dt;
    return change <= static_cast<double>(std::numeric_limits<float>::max()) ? static_cast<float>(change) : NAN;
}

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
    frame.sourceId = update.sourceId;
    frame.sourceAvailable = update.sourceAvailable;
    frame.colourSpace = update.colourSpace;
    frame.magnitudes.assign(update.magnitudes.begin(), update.magnitudes.end());
    frame.phases.assign(update.phases.begin(), update.phases.end());
    frame.frequencies.assign(update.frequencies.begin(), update.frequencies.end());
    frame.sourceContributions = update.sourceContributions;
    frame.presentedContributions = update.presentedContributions;
    frame.status = update.status;
    frame.smoothing = update.smoothing;
    frame.smoothing.amount = std::isfinite(frame.smoothing.amount) ? std::clamp(frame.smoothing.amount, 0.0f, 1.0f) : 0.6f;
    const auto response = [&](float value) {
        return frame.smoothing.enabled && std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
    };
    frame.smoothing.responseSeconds = response(frame.smoothing.responseSeconds);
    frame.smoothing.minimumResponseSeconds = response(frame.smoothing.minimumResponseSeconds);
    frame.smoothing.maximumResponseSeconds = response(frame.smoothing.maximumResponseSeconds);
    frame.sourceAnalysis = analyseColour({update.colourResult.X, update.colourResult.Y, update.colourResult.Z}, update.sourceAvailable);
    frame.presentedAnalysis = analyseColour(update.presentedXYZ, validXYZ(update.presentedXYZ));
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

    frame.sourceColour.displayR = update.colourResult.r;
    frame.sourceColour.displayG = update.colourResult.g;
    frame.sourceColour.displayB = update.colourResult.b;
    frame.sourceColour.cieX = update.colourResult.X;
    frame.sourceColour.cieY = update.colourResult.Y;
    frame.sourceColour.cieZ = update.colourResult.Z;
    if (frame.sourceAnalysis.available)
        ColourCore::XYZtoRGB(frame.sourceColour.cieX, frame.sourceColour.cieY, frame.sourceColour.cieZ,
            frame.sourceColour.linear.r, frame.sourceColour.linear.g, frame.sourceColour.linear.b,
            update.colourSpace, false, update.applyGamutMapping);
    ColourCore::XYZtoOklab(frame.sourceColour.cieX, frame.sourceColour.cieY, frame.sourceColour.cieZ,
        frame.sourceColour.oklabL, frame.sourceColour.oklabA, frame.sourceColour.oklabB);
    frame.colour.displayR = update.displayColour.r;
    frame.colour.displayG = update.displayColour.g;
    frame.colour.displayB = update.displayColour.b;
    frame.colour.cieX = std::isfinite(update.presentedXYZ.X) ? update.presentedXYZ.X : update.colourResult.X;
    frame.colour.cieY = std::isfinite(update.presentedXYZ.Y) ? update.presentedXYZ.Y : update.colourResult.Y;
    frame.colour.cieZ = std::isfinite(update.presentedXYZ.Z) ? update.presentedXYZ.Z : update.colourResult.Z;
    if (frame.presentedAnalysis.available)
        ColourCore::XYZtoRGB(frame.colour.cieX, frame.colour.cieY, frame.colour.cieZ,
            frame.colour.linear.r, frame.colour.linear.g, frame.colour.linear.b,
            update.colourSpace, false, update.applyGamutMapping);
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

void OSCFrameBuilder::updateBands(OSCFrameData& frame, const OSCFrameUpdate& update, bool sameSource) {
    const double dt = sameSource ? update.sourceTimestamp - previousTimestamp_ : std::numeric_limits<double>::quiet_NaN();
    const double duration = frame.meta.sampleRate > 0 && frame.meta.fftSize > 0 ?
        static_cast<double>(frame.meta.fftSize) / frame.meta.sampleRate : 0.0;
    if (!frame.sourceAvailable || !frame.sourceContributions || !std::isfinite(update.sourceTimestamp)) {
        bandHistory_ = {};
        bandData_ = {};
        return;
    }
    const bool initial = !bandData_.available || !std::isfinite(dt) || dt < 0.0 || dt > std::max(0.5, duration * 4.0);
    if (!initial && dt == 0.0) {
        frame.bands = bandData_;
        return;
    }
    if (initial) bandHistory_ = {};
    std::array<double, ColourPresentation::contributionBandCount> magnitudes{}, responses{};
    for (const auto& band : *frame.sourceContributions)
        if (band.band < magnitudes.size() && std::isfinite(band.magnitude))
            magnitudes[band.band] += static_cast<double>(std::max(0.0f, band.magnitude));
    if (frame.presentedContributions && frame.smoothing.enabled) {
        for (const auto& band : *frame.presentedContributions)
            if (band.band < responses.size() && std::isfinite(band.responseSeconds))
                responses[band.band] = static_cast<double>(std::max(0.0f, band.responseSeconds));
    }
    const double weight = initial ? 0.0 : -std::expm1(-dt / 1.5);
    static const double onsetMagnitudeFloor = std::pow(10.0, static_cast<double>(kBandLevelMinimumDb) / 20.0);
    for (std::size_t i = 0; i < magnitudes.size(); ++i) {
        auto& history = bandHistory_[i];
        const double magnitude = magnitudes[i];
        const double total = std::max(magnitude, history.magnitude);
        const double flux = !initial && total > 0.0 ?
            std::clamp(std::max(0.0, magnitude - history.magnitude) / total * (duration > 0.0 ? duration / dt : 1.0), 0.0, 1.0) : 0.0;
        bandData_.onsetDetected[i] = !initial && history.magnitude > onsetMagnitudeFloor &&
            history.previousFlux > history.precedingFlux && history.previousFlux >= flux &&
            history.previousFlux > history.previousThreshold ? 1.0f : 0.0f;
        const double difference = flux - history.meanFlux;
        history.meanFlux += weight * difference;
        history.varianceFlux = (1.0 - weight) * (history.varianceFlux + weight * difference * difference);
        history.previousThreshold = history.meanFlux + std::sqrt(history.varianceFlux);
        history.precedingFlux = history.previousFlux;
        history.previousFlux = flux;
        history.smoothedMagnitude = initial ? magnitude : std::lerp(magnitude, history.smoothedMagnitude,
            smoothingRetention(responses[i], dt));
        history.magnitude = magnitude;
        bandData_.smoothedMagnitudes[i] = static_cast<float>(history.smoothedMagnitude);
        bandData_.spectralFlux[i] = static_cast<float>(flux);
    }
    bandData_.available = true;
    frame.bands = bandData_;
}

OSCFrameData OSCFrameBuilder::build(const OSCFrameUpdate& update) {
    auto frame = buildFrameData(update);
    const bool sameSource = sourceId_ == update.sourceId && status_ == update.status &&
        sampleRate_ == frame.meta.sampleRate && fftSize_ == frame.meta.fftSize &&
        smoothing_.enabled == frame.smoothing.enabled && smoothing_.manual == frame.smoothing.manual &&
        smoothing_.amount == frame.smoothing.amount;
    const bool sameBandSource = sourceId_ == update.sourceId && sampleRate_ == frame.meta.sampleRate && fftSize_ == frame.meta.fftSize &&
        smoothing_.enabled == frame.smoothing.enabled && smoothing_.manual == frame.smoothing.manual && smoothing_.amount == frame.smoothing.amount;
    updateBands(frame, update, sameBandSource);
    const double dt = sameSource ? update.sourceTimestamp - previousTimestamp_ : std::numeric_limits<double>::quiet_NaN();
    const ColourCore::XYZ source{update.colourResult.X, update.colourResult.Y, update.colourResult.Z};
    if (frame.sourceAnalysis.available) frame.sourceAnalysis.colourChangePerSecond = colourChange(previousSource_, source, dt);
    if (frame.presentedAnalysis.available) frame.presentedAnalysis.colourChangePerSecond = colourChange(previousPresented_, update.presentedXYZ, dt);
    previousSource_ = frame.sourceAnalysis.available ? source : ColourCore::XYZ{NAN, NAN, NAN};
    previousPresented_ = frame.presentedAnalysis.available ? update.presentedXYZ : ColourCore::XYZ{NAN, NAN, NAN};
    previousTimestamp_ = update.sourceTimestamp;
    sourceId_ = update.sourceId;
    status_ = update.status;
    smoothing_ = frame.smoothing;
    sampleRate_ = frame.meta.sampleRate;
    fftSize_ = frame.meta.fftSize;
    return frame;
}

void OSCFrameBuilder::reset() {
    *this = OSCFrameBuilder{};
}

}

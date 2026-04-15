#include "osc_frame_builder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace Synesthesia::OSC {

namespace {

float interpolateMagnitude(const std::span<const float> magnitudes,
                           const float dominantFrequency,
                           const float sampleRate) {
    if (magnitudes.size() < 2 || dominantFrequency <= 0.0f || sampleRate <= 0.0f) {
        return 0.0f;
    }

    const float fftSize = 2.0f * static_cast<float>(magnitudes.size() - 1);
    const float binFrequency = sampleRate / fftSize;
    const float floatingIndex = dominantFrequency / binFrequency;
    const auto indexFloor = static_cast<std::size_t>(floatingIndex);
    const auto indexCeil = indexFloor + 1;
    if (indexCeil >= magnitudes.size()) {
        return 0.0f;
    }

    const float fraction = floatingIndex - static_cast<float>(indexFloor);
    return (1.0f - fraction) * magnitudes[indexFloor] + fraction * magnitudes[indexCeil];
}

float interpolatePhase(const std::span<const float> magnitudes,
                       const std::span<const float> phases,
                       const float dominantFrequency,
                       const float sampleRate) {
    if (magnitudes.size() < 2 || phases.size() != magnitudes.size() ||
        dominantFrequency <= 0.0f || sampleRate <= 0.0f) {
        return 0.0f;
    }

    const float fftSize = 2.0f * static_cast<float>(magnitudes.size() - 1);
    const float binFrequency = sampleRate / fftSize;
    const float floatingIndex = dominantFrequency / binFrequency;
    const auto indexFloor = static_cast<std::size_t>(floatingIndex);
    const auto indexCeil = indexFloor + 1;
    if (indexCeil >= phases.size()) {
        return 0.0f;
    }

    const float fraction = floatingIndex - static_cast<float>(indexFloor);
    const float phaseA = phases[indexFloor];
    const float phaseB = phases[indexCeil];
    float phaseDifference = phaseB - phaseA;

    constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
    if (phaseDifference > std::numbers::pi_v<float>) {
        phaseDifference -= twoPi;
    } else if (phaseDifference < -std::numbers::pi_v<float>) {
        phaseDifference += twoPi;
    }

    const float interpolatedPhase = phaseA + fraction * phaseDifference;
    return std::fmod(interpolatedPhase + std::numbers::pi_v<float>, twoPi) - std::numbers::pi_v<float>;
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
        : static_cast<int32_t>(magnitudes.size() * 2);
}

}

OSCAnalysisSignals buildAnalysisSignals(const AudioProcessor::SpectralData& spectralData) {
    return buildAnalysisSignals(
        spectralData.momentaryLoudnessLUFS,
        spectralData.spectralFlux,
        spectralData.onsetDetected);
}

OSCAnalysisSignals buildAnalysisSignals(const float momentaryLoudnessLUFS,
                                        const float spectralFlux,
                                        const bool onsetDetected) {
    OSCAnalysisSignals signals{};
    signals.momentaryLoudnessLUFS = momentaryLoudnessLUFS;
    signals.spectralFlux = spectralFlux;
    signals.onsetDetected = onsetDetected;
    return signals;
}

OSCSmoothingSignals buildSmoothingSignals(const SmoothingSignalFeatures& features) {
    OSCSmoothingSignals signals{};
    signals.onsetDetected = features.onsetDetected;
    signals.spectralFlux = features.spectralFlux;
    signals.spectralFlatness = features.spectralFlatness;
    signals.loudnessNormalised = features.loudnessNormalised;
    signals.brightnessNormalised = features.brightnessNormalised;
    signals.spectralSpreadNorm = features.spectralSpreadNorm;
    signals.spectralRolloffNorm = features.spectralRolloffNorm;
    signals.spectralCrestNorm = features.spectralCrestNorm;
    signals.phaseInstabilityNorm = features.phaseInstabilityNorm;
    signals.phaseCoherenceNorm = features.phaseCoherenceNorm;
    signals.phaseTransientNorm = features.phaseTransientNorm;
    return signals;
}

OSCFrameData buildFrameData(const OSCFrameUpdate& update) {
    OSCFrameData frame{};
    const auto spectralCharacteristics = ColourCore::calculateSpectralCharacteristics(
        update.magnitudes,
        update.sampleRate);
    frame.meta.sampleRate = static_cast<int32_t>(update.sampleRate);
    frame.meta.fftSize = resolveFftSize(update.magnitudes);
    frame.meta.frameTimestamp = currentTimestampMicros();

    frame.signal.dominantFrequencyHz = update.colourResult.dominantFrequency;
    frame.signal.dominantWavelengthNm = update.colourResult.dominantWavelength;
    frame.signal.visualiserMagnitude = interpolateMagnitude(
        update.magnitudes,
        update.colourResult.dominantFrequency,
        update.sampleRate);
    frame.signal.phaseRadians = interpolatePhase(
        update.magnitudes,
        update.phases,
        update.colourResult.dominantFrequency,
        update.sampleRate);

    frame.colour.displayR = update.displayColour.r;
    frame.colour.displayG = update.displayColour.g;
    frame.colour.displayB = update.displayColour.b;
    frame.colour.cieX = update.colourResult.X;
    frame.colour.cieY = update.colourResult.Y;
    frame.colour.cieZ = update.colourResult.Z;
    frame.colour.oklabL = update.colourResult.L;
    frame.colour.oklabA = update.colourResult.a;
    frame.colour.oklabB = update.colourResult.b_comp;

    frame.spectral.flatness = update.colourResult.spectralFlatness;
    frame.spectral.centroidHz = update.colourResult.spectralCentroid;
    frame.spectral.spreadHz = update.colourResult.spectralSpread;
    frame.spectral.normalisedSpread = spectralCharacteristics.normalisedSpread;
    frame.spectral.rolloffHz = update.colourResult.spectralRolloff;
    frame.spectral.crestFactor = update.colourResult.spectralCrestFactor;
    frame.spectral.spectralFlux = update.analysisSignals.spectralFlux;

    frame.loudness.loudnessDb = update.colourResult.loudnessDb;
    frame.loudness.loudnessNormalised = update.colourResult.loudnessNormalised;
    frame.loudness.frameLoudnessDb = update.colourResult.frameLoudnessDb;
    frame.loudness.momentaryLoudnessLUFS = update.analysisSignals.momentaryLoudnessLUFS;
    frame.loudness.estimatedSPL = update.colourResult.estimatedSPL;
    frame.loudness.luminanceCdM2 = update.colourResult.luminanceCdM2;
    frame.loudness.brightnessNormalised = update.colourResult.brightnessNormalised;

    frame.transient.transientMix = update.colourResult.transientMix;
    frame.transient.onsetDetected = update.analysisSignals.onsetDetected;

    frame.phase.instabilityNorm = update.colourResult.phaseInstabilityNorm;
    frame.phase.coherenceNorm = update.colourResult.phaseCoherenceNorm;
    frame.phase.transientNorm = update.colourResult.phaseTransientNorm;

    frame.smoothing = update.smoothingSignals;
    return frame;
}

}

#include "resyne/encoding/formats/rsyn_presentation.h"

#include <algorithm>
#include <cmath>

#include "audio/analysis/presentation/sample_sequence.h"
#include "audio/analysis/presentation/spectral_presentation.h"
#include "ui/smoothing/smoothing.h"
#include "ui/smoothing/smoothing_features.h"

namespace RSYNPresentation {

namespace {

SpectralPresentation::Settings buildPresentationSettings(const RSYNPresentationSettings& settings) {
    SpectralPresentation::Settings presentation{};
    presentation.lowGain = settings.lowGain;
    presentation.midGain = settings.midGain;
    presentation.highGain = settings.highGain;
    presentation.colourSpace = settings.colourSpace;
    presentation.applyGamutMapping = settings.applyGamutMapping;
    return presentation;
}

RSYNSmoothingSignals copySignals(const SmoothingSignalFeatures& features) {
    RSYNSmoothingSignals output{};
    output.onsetDetected = features.onsetDetected;
    output.spectralFlux = features.spectralFlux;
    output.spectralFlatness = features.spectralFlatness;
    output.loudnessNormalised = features.loudnessNormalised;
    output.brightnessNormalised = features.brightnessNormalised;
    output.spectralSpreadNorm = features.spectralSpreadNorm;
    output.spectralRolloffNorm = features.spectralRolloffNorm;
    output.spectralCrestNorm = features.spectralCrestNorm;
    output.phaseInstabilityNorm = features.phaseInstabilityNorm;
    output.phaseCoherenceNorm = features.phaseCoherenceNorm;
    output.phaseTransientNorm = features.phaseTransientNorm;
    return output;
}

std::array<float, 3> xyzToOklab(const ColourCore::FrameResult& result) {
    std::array<float, 3> oklab{};
    ColourCore::XYZtoOklab(result.X, result.Y, result.Z, oklab[0], oklab[1], oklab[2]);
    return oklab;
}

void writeSmoothedOutputs(RSYNPresentationFrame& frame,
                          const std::array<float, 3>& smoothedOklab,
                          const RSYNPresentationSettings& settings) {
    frame.smoothedOklab = smoothedOklab;

    float smoothedX = 0.0f;
    float smoothedY = 0.0f;
    float smoothedZ = 0.0f;
    ColourCore::OklabtoXYZ(
        smoothedOklab[0],
        smoothedOklab[1],
        smoothedOklab[2],
        smoothedX,
        smoothedY,
        smoothedZ);

    ColourCore::XYZtoLab(
        smoothedX,
        smoothedY,
        smoothedZ,
        frame.smoothedLab[0],
        frame.smoothedLab[1],
        frame.smoothedLab[2]);

    const auto smoothedRgb = SpectralPresentation::displayRGBFromXYZ(
        smoothedX,
        smoothedY,
        smoothedZ,
        buildPresentationSettings(settings));
    frame.smoothedDisplayRgb = {smoothedRgb[0], smoothedRgb[1], smoothedRgb[2]};
}

}

struct ReplayState::Impl {
    RSYNPresentationSettings settings;
    SpectralPresentation::Settings presentationSettings;
    SpringSmoother smoother;
    UI::Smoothing::MagnitudeHistory fluxHistory;
    AudioColourSample previous;
    bool hasPrevious = false;
    explicit Impl(const RSYNPresentationSettings& config)
        : settings(config), presentationSettings(buildPresentationSettings(config)),
          smoother(8.0f, 1.0f, config.springMass) {
        smoother.setSmoothingAmount(config.smoothingAmount);
    }
};

ReplayState::ReplayState(const RSYNPresentationSettings& settings)
    : state(std::make_unique<Impl>(settings)) {}
ReplayState::~ReplayState() = default;

RSYNPresentationFrame ReplayState::process(const AudioColourSample& sample) {
    auto& s = *state;
    const auto prepared = SpectralPresentation::SampleSequence::prepareSampleFrame(
        sample, s.presentationSettings, s.hasPrevious ? &s.previous : nullptr);
    RSYNPresentationFrame frame{};
    frame.timestamp = sample.timestamp;
    frame.analysis = prepared.colourResult;
    frame.targetOklab = xyzToOklab(prepared.colourResult);
    auto features = UI::Smoothing::buildSignalFeatures(prepared.colourResult);
    UI::Smoothing::updateFluxHistory(prepared.visualiserMagnitudes, s.fluxHistory, features);
    frame.smoothingSignals = copySignals(features);
    if (!s.settings.smoothingEnabled) {
        writeSmoothedOutputs(frame, frame.targetOklab, s.settings);
    } else {
        if (!s.hasPrevious) {
            s.smoother.resetOklab(frame.targetOklab[0], frame.targetOklab[1], frame.targetOklab[2]);
        } else {
            const double delta = sample.timestamp - s.previous.timestamp;
            const float dt = std::isfinite(delta) && delta > 0.0 ? static_cast<float>(delta)
                : SpectralPresentation::SampleSequence::kFallbackDeltaTimeSeconds;
            s.smoother.setTargetOklab(frame.targetOklab[0], frame.targetOklab[1], frame.targetOklab[2]);
            if (s.settings.manualSmoothing) s.smoother.update(dt * s.settings.smoothingUpdateFactor);
            else s.smoother.update(dt * s.settings.smoothingUpdateFactor, features);
        }
        std::array<float, 3> smoothed{};
        s.smoother.getCurrentOklab(smoothed[0], smoothed[1], smoothed[2]);
        writeSmoothedOutputs(frame, smoothed, s.settings);
    }
    s.previous = sample;
    s.hasPrevious = true;
    return frame;
}

std::shared_ptr<RSYNPresentationData> buildPresentationData(
    const std::vector<AudioColourSample>& samples,
    const RSYNPresentationSettings& settings,
    const std::function<void(float)>& progress) {
    auto presentation = std::make_shared<RSYNPresentationData>();
    presentation->settings = settings;
    presentation->frames.reserve(samples.size());
    ReplayState replay(settings);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        presentation->frames.push_back(replay.process(samples[index]));
        if (progress) progress(static_cast<float>(index + 1) / static_cast<float>(samples.size()));
    }
    if (samples.empty() && progress) progress(1.0f);
    return presentation;
}

}

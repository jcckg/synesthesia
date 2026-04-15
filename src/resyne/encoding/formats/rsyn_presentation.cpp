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

std::shared_ptr<RSYNPresentationData> buildPresentationData(
    const std::vector<AudioColourSample>& samples,
    const RSYNPresentationSettings& settings,
    const std::function<void(float)>& progress) {
    auto presentation = std::make_shared<RSYNPresentationData>();
    presentation->settings = settings;
    presentation->frames.reserve(samples.size());

    if (samples.empty()) {
        if (progress) {
            progress(1.0f);
        }
        return presentation;
    }

    SpringSmoother smoother(8.0f, 1.0f, settings.springMass);
    smoother.setSmoothingAmount(settings.smoothingAmount);

    const auto presentationSettings = buildPresentationSettings(settings);
    UI::Smoothing::MagnitudeHistory fluxHistory;

    for (std::size_t index = 0; index < samples.size(); ++index) {
        const AudioColourSample& sample = samples[index];
        const AudioColourSample* previousSample = index > 0 ? &samples[index - 1] : nullptr;
        const auto preparedFrame = SpectralPresentation::SampleSequence::prepareSampleFrame(
            sample,
            presentationSettings,
            previousSample);

        RSYNPresentationFrame frame{};
        frame.timestamp = sample.timestamp;
        frame.analysis = preparedFrame.colourResult;
        frame.targetOklab = xyzToOklab(preparedFrame.colourResult);

        auto features = UI::Smoothing::buildSignalFeatures(preparedFrame.colourResult);
        UI::Smoothing::updateFluxHistory(preparedFrame.visualiserMagnitudes, fluxHistory, features);
        frame.smoothingSignals = copySignals(features);

        if (!settings.smoothingEnabled) {
            writeSmoothedOutputs(frame, frame.targetOklab, settings);
            presentation->frames.push_back(std::move(frame));
        } else {
            if (index == 0) {
                smoother.resetOklab(frame.targetOklab[0], frame.targetOklab[1], frame.targetOklab[2]);
            } else {
                const double deltaSeconds = sample.timestamp - samples[index - 1].timestamp;
                const float deltaTime = std::isfinite(deltaSeconds) && deltaSeconds > 0.0
                    ? static_cast<float>(deltaSeconds)
                    : SpectralPresentation::SampleSequence::kFallbackDeltaTimeSeconds;
                smoother.setTargetOklab(frame.targetOklab[0], frame.targetOklab[1], frame.targetOklab[2]);
                if (settings.manualSmoothing) {
                    smoother.update(deltaTime * settings.smoothingUpdateFactor);
                } else {
                    smoother.update(deltaTime * settings.smoothingUpdateFactor, features);
                }
            }

            std::array<float, 3> smoothedOklab{};
            smoother.getCurrentOklab(smoothedOklab[0], smoothedOklab[1], smoothedOklab[2]);
            writeSmoothedOutputs(frame, smoothedOklab, settings);
            presentation->frames.push_back(std::move(frame));
        }

        if (progress) {
            progress(static_cast<float>(index + 1) / static_cast<float>(samples.size()));
        }
    }

    return presentation;
}

}

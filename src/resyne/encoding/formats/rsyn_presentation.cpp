#include "resyne/encoding/formats/rsyn_presentation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "audio/analysis/presentation/contribution_history.h"
#include "colour/colour_track.h"

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

}

struct ReplayState::Impl {
    RSYNPresentationSettings settings;
    SpectralPresentation::Settings presentationSettings;
    SpectralPresentation::ContributionHistory contributions;
    AudioColourSample previous;
    bool hasPrevious = false;
    explicit Impl(const RSYNPresentationSettings& config)
        : settings(config), presentationSettings(buildPresentationSettings(config)) {
        if (config.pipelineId != RSYN_PRESENTATION_PIPELINE_ID)
            throw std::invalid_argument("Unsupported presentation pipeline.");
    }
};

ReplayState::ReplayState(const RSYNPresentationSettings& settings)
    : state(std::make_unique<Impl>(settings)) {}
ReplayState::~ReplayState() = default;

RSYNPresentationFrame ReplayState::analyse(const AudioColourSample& sample, SpectralPresentation::PreparedFrame* preparedOutput) {
    auto& s = *state;
    auto prepared = SpectralPresentation::SampleSequence::prepareSampleFrame(
        sample, s.presentationSettings, s.hasPrevious ? &s.previous : nullptr);
    RSYNPresentationFrame frame{};
    frame.timestamp = sample.timestamp;
    frame.analysis = prepared.colourResult;
    frame.contributionTargets = prepared.contributions;
    frame.frequencyResolution = prepared.frequencyResolution;
    frame.targetOklab = xyzToOklab(prepared.colourResult);
    auto features = UI::Smoothing::buildSignalFeatures(prepared.colourResult);
    frame.smoothingSignals = copySignals(features);
    if (preparedOutput) *preparedOutput = std::move(prepared);
    s.previous = sample;
    s.hasPrevious = true;
    return frame;
}

RSYNPresentationFrame ReplayState::process(const AudioColourSample& sample, SpectralPresentation::PreparedFrame* preparedOutput) {
    return processAnalysed(analyse(sample, preparedOutput));
}

RSYNPresentationFrame ReplayState::processAnalysed(const RSYNPresentationFrame& input) {
    auto& s = *state;
    auto frame = input;
    const auto rawRgb = ColourPresentation::projectXYZ({static_cast<double>(frame.analysis.X),
        static_cast<double>(frame.analysis.Y), static_cast<double>(frame.analysis.Z)}, s.settings.colourSpace, s.settings.applyGamutMapping);
    frame.analysis.r = rawRgb.r; frame.analysis.g = rawRgb.g; frame.analysis.b = rawRgb.b;
    if (!frame.contributionTargets) throw std::runtime_error("Spectral contributions are unavailable for this presentation.");
    const auto& presented = s.contributions.process(*frame.contributionTargets, frame.timestamp,
        s.settings.smoothingEnabled, s.settings.manualSmoothing, s.settings.smoothingAmount, frame.analysis.spectralFlatness, frame.frequencyResolution);
    frame.presentedContributions = std::make_shared<const ColourPresentation::SpectralContributions>(presented);
    frame.presentedXYZ = s.contributions.xyz();
    frame.responseSeconds = s.contributions.responseSeconds();
    frame.smoothingSignals.spectralFlux = s.contributions.spectralFlux();
    frame.smoothingSignals.onsetDetected = s.contributions.onsetDetected();
    ColourCore::XYZtoOklab(frame.presentedXYZ[0], frame.presentedXYZ[1], frame.presentedXYZ[2],
        frame.smoothedOklab[0], frame.smoothedOklab[1], frame.smoothedOklab[2]);
    ColourCore::XYZtoLab(frame.presentedXYZ[0], frame.presentedXYZ[1], frame.presentedXYZ[2],
        frame.smoothedLab[0], frame.smoothedLab[1], frame.smoothedLab[2]);
    const auto rgb = ColourPresentation::projectXYZ(ColourPresentation::toColourTriple(frame.presentedXYZ),
        s.settings.colourSpace, s.settings.applyGamutMapping);
    frame.smoothedDisplayRgb = {rgb.r, rgb.g, rgb.b};
    return frame;
}

void ReplayState::setSmoothingSettings(bool enabled, bool manual, float amount) {
    state->settings.smoothingEnabled = enabled;
    state->settings.manualSmoothing = manual;
    state->settings.smoothingAmount = amount;
}

void ReplayState::setOutputSettings(ColourCore::ColourSpace colourSpace, bool gamutMapping) {
    state->settings.colourSpace = colourSpace;
    state->settings.applyGamutMapping = gamutMapping;
    state->presentationSettings.colourSpace = colourSpace;
    state->presentationSettings.applyGamutMapping = gamutMapping;
}

void ReplayState::seedPresentation(const RSYNPresentationFrame& frame) {
    if (frame.presentedContributions) state->contributions.seed(*frame.presentedContributions);

}

double durationSeconds(const AudioMetadata& metadata, double lastTimestamp) {
    if (metadata.originalSampleCount > 0 && std::isfinite(metadata.sampleRate) && metadata.sampleRate > 0.0f)
        return static_cast<double>(metadata.originalSampleCount) / static_cast<double>(metadata.sampleRate);
    if (std::isfinite(metadata.durationSeconds) && metadata.durationSeconds > 0.0) return metadata.durationSeconds;
    if (std::isfinite(lastTimestamp) && lastTimestamp > 0.0) return lastTimestamp;
    if (metadata.sampleRate > 0.0f && metadata.hopSize > 0 && metadata.numFrames > 0)
        return static_cast<double>(metadata.numFrames) * static_cast<double>(metadata.hopSize) / static_cast<double>(metadata.sampleRate);
    return 0.0;
}

std::shared_ptr<const ColourPresentation::ColourTrack> colourTrack(const RSYNPresentationData& presentation,
                                                                  bool smoothed) {
    if (presentation.settings.pipelineId != RSYN_PRESENTATION_PIPELINE_ID)
        throw std::invalid_argument("Unsupported presentation pipeline.");
    ColourPresentation::ColourTrack track;
    for (const auto& frame : presentation.frames) {
        ColourPresentation::TrackPoint point{frame.timestamp};
        const auto xyz = smoothed ? frame.presentedXYZ : std::array<float, 3>{frame.analysis.X, frame.analysis.Y, frame.analysis.Z};
        point.colour = ColourPresentation::toColourTriple(xyz);
        point.responseSeconds = smoothed ? frame.responseSeconds : 0.0f;
        track.append(point);
    }
    return track.snapshot();
}

std::shared_ptr<RSYNPresentationData> buildPresentationData(
    const std::vector<AudioColourSample>& samples,
    const RSYNPresentationSettings& settings,
    const std::function<void(float)>& progress) {
    auto presentation = std::make_shared<RSYNPresentationData>();
    presentation->settings = settings;
    presentation->frames.reserve(samples.size());
    ReplayState replay(presentation->settings);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        presentation->frames.push_back(replay.process(samples[index]));
        if (progress) progress(static_cast<float>(index + 1) / static_cast<float>(samples.size()));
    }
    if (samples.empty() && progress) progress(1.0f);
    return presentation;
}

}

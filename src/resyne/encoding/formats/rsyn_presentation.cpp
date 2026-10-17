#include "resyne/encoding/formats/rsyn_presentation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "audio/analysis/presentation/contribution_history.h"
#include "audio/analysis/presentation/spectral_envelope.h"
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

}

struct ReplayState::Impl {
    RSYNPresentationSettings settings;
    SpectralPresentation::Settings presentationSettings;
    SpectralPresentation::ContributionHistory contributions;
    SpectralPresentation::SpectralEnvelope envelope;
    SpectralPresentation::Frame previous;
    double previousTimestamp = 0;
    bool hasPrevious = false;
    explicit Impl(const RSYNPresentationSettings& config)
        : settings(config), presentationSettings(buildPresentationSettings(config)) {}
};

ReplayState::ReplayState(const RSYNPresentationSettings& settings)
    : state(std::make_unique<Impl>(settings)) {}
ReplayState::~ReplayState() = default;

RSYNPresentationFrame ReplayState::analyse(const AudioColourSample& sample, SpectralPresentation::PreparedFrame* preparedOutput) {
    auto& s = *state;
    auto source = SpectralPresentation::SampleSequence::buildFrame(sample);
    auto prepared = SpectralPresentation::prepareFrame(source, s.presentationSettings,
        std::isfinite(sample.loudnessLUFS) ? sample.loudnessLUFS : ColourCore::LOUDNESS_DB_UNSPECIFIED,
        s.hasPrevious ? &s.previous : nullptr,
        s.hasPrevious ? static_cast<float>(sample.timestamp - s.previousTimestamp) :
            SpectralPresentation::SampleSequence::kFallbackDeltaTimeSeconds);
    RSYNPresentationFrame frame{};
    frame.timestamp = sample.timestamp;
    frame.analysis = prepared.colourResult;
    frame.contributionTargets = prepared.contributions;
    frame.frequencyResolution = prepared.frequencyResolution;
    const auto estimatedSource = s.envelope.process(source, sample.timestamp, s.presentationSettings);
    auto estimated = SpectralPresentation::prepareFrame(estimatedSource, s.presentationSettings, sample.loudnessLUFS);
    auto automatic = std::make_shared<ColourPresentation::SpectralContributions>(*estimated.contributions);
    const auto& responses = s.envelope.responses();
    double weightedResponse = 0, presence = 0;
    for (auto& band : *automatic) {
        const double frequency = ColourPresentation::contributionBandFrequency(band.band);
        double times = 0, mass = 0;
        std::size_t first = 0, last = responses.size();
        if (estimatedSource.frequencies.size() != responses.size() && prepared.frequencyResolution > 0) {
            const double position = std::floor(frequency / prepared.frequencyResolution);
            first = static_cast<std::size_t>(std::clamp(position - 2, 0.0, static_cast<double>(last)));
            last = static_cast<std::size_t>(std::clamp(position + 3, 0.0, static_cast<double>(last)));
        }
        for (std::size_t index = first; index < last; ++index) {
            const double sourceFrequency = estimatedSource.frequencies.size() == responses.size() ?
                estimatedSource.frequencies[index] : index * static_cast<double>(prepared.frequencyResolution);
            const double weight = estimatedSource.magnitudes[index] * std::max(0.0,
                1 - std::abs(sourceFrequency - frequency) / prepared.frequencyResolution);
            times += weight * responses[index];
            mass += weight;
        }
        band.responseSeconds = mass > 0 ? static_cast<float>(times / mass) : 0;
        const double size = std::abs(band.xyz[0]) + std::abs(band.xyz[1]) + std::abs(band.xyz[2]);
        weightedResponse += size * band.responseSeconds;
        presence += size;
    }
    frame.automaticContributions = std::move(automatic);
    frame.automaticResponseSeconds = presence > 0 ? static_cast<float>(weightedResponse / presence) : 0;
    auto features = UI::Smoothing::buildSignalFeatures(prepared.colourResult);
    frame.smoothingSignals = copySignals(features);
    if (preparedOutput) *preparedOutput = std::move(prepared);
    s.previous = std::move(source);
    s.previousTimestamp = sample.timestamp;
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
    const bool automatic = s.settings.smoothingEnabled && !s.settings.manualSmoothing;
    if (automatic && !frame.automaticContributions)
        throw std::runtime_error("Source spectra are required to rebuild automatic smoothing.");
    const auto& targets = automatic ? *frame.automaticContributions : *frame.contributionTargets;
    const auto& presented = s.contributions.process(targets, frame.timestamp,
        s.settings.smoothingEnabled, s.settings.manualSmoothing, s.settings.smoothingAmount, frame.frequencyResolution);
    frame.presentedContributions = automatic ? frame.automaticContributions :
        std::make_shared<const ColourPresentation::SpectralContributions>(presented);
    frame.presentedXYZ = s.contributions.xyz();
    frame.responseSeconds = automatic ? frame.automaticResponseSeconds : s.contributions.responseSeconds();
    frame.smoothingSignals.spectralFlux = s.contributions.spectralFlux();
    frame.smoothingSignals.onsetDetected = s.contributions.onsetDetected();
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

void ReplayState::setEqualiserSettings(float low, float mid, float high) {
    state->settings.lowGain = state->presentationSettings.lowGain = low;
    state->settings.midGain = state->presentationSettings.midGain = mid;
    state->settings.highGain = state->presentationSettings.highGain = high;
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

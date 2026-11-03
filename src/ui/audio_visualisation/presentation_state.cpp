#include "ui/audio_visualisation/presentation_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <vector>

#include "audio/analysis/presentation/sample_sequence.h"
#include "audio/analysis/phase/phase_features.h"
#include "audio/analysis/presentation/spectral_presentation.h"
#include "colour/colour_core.h"
#include "colour/colour_presentation.h"
#include "colour/colour_track.h"
#include "resyne/controller/controller.h"
#include "resyne/recorder/colour_cache_utils.h"
#include "resyne/recorder/presentation_worker.h"
#include "resyne/ui/timeline/timeline_gradient.h"

#ifdef ENABLE_OSC
#include "synesthesia_osc_integration.h"
#endif

namespace UI::AudioVisualisation {

namespace {
#ifdef ENABLE_OSC
SpectralPresentation::Settings buildPresentationSettings(const UIState& state,
                                                         const float lowGain,
                                                         const float midGain,
                                                         const float highGain) {
    SpectralPresentation::Settings settings{};
    settings.lowGain = lowGain;
    settings.midGain = midGain;
    settings.highGain = highGain;
    settings.colourSpace = state.visualSettings.colourSpace;
    settings.applyGamutMapping = state.visualSettings.gamutMappingEnabled;
    return settings;
}

SpectralPresentation::Settings buildPlaybackPresentationSettings(const UIState& state,
                                                                 const ReSyne::RecorderState& recorderState) {
    return buildPresentationSettings(
        state,
        recorderState.importLowGain,
        recorderState.importMidGain,
        recorderState.importHighGain);
}

void sendOSCFrame(UIState& state, const RSYNPresentationSettings& settings,
                  const std::vector<float>& magnitudes,
                  const SpectralPresentation::Frame& frame,
                  const ColourCore::FrameResult& colourResult,
                  const float displayR,
                  const float displayG,
                  const float displayB,
                  const Synesthesia::OSC::OSCAnalysisSignals& analysisSignals,
                  const ColourCore::XYZ& presentedXYZ, double timestamp, float responseSeconds,
                  ColourPresentation::ResponseRange responseRange, uint64_t sourceId,
                  std::shared_ptr<const ColourPresentation::SpectralContributions> sourceContributions,
                  std::shared_ptr<const ColourPresentation::SpectralContributions> presentedContributions, int fftSize = 0) {
    Synesthesia::OSC::OSCFrameUpdate update{};
    const auto& recorder = state.resyneState.recorderState;
    update.status = Synesthesia::OSC::playbackStatus(recorder.importPhase != 0, recorder.isRecording,
        recorder.audioOutput && recorder.audioOutput->getTotalFrames() > 0 ? recorder.audioOutput->isPlaying() : true);
    update.sourceId = sourceId;
    update.sourceAvailable = recorder.importPhase == 0;
    update.smoothing = {settings.smoothingEnabled, settings.manualSmoothing, settings.smoothingAmount,
        responseSeconds, responseRange.minimum, responseRange.maximum};
    update.magnitudes = std::span<const float>(magnitudes.data(), magnitudes.size());
    update.phases = std::span<const float>(frame.phases.data(), frame.phases.size());
    update.frequencies = frame.frequencies;
    update.fftSize = fftSize;
    update.sampleRate = frame.sampleRate;
    update.colourResult = colourResult;
    update.displayColour = ColourCore::RGB{displayR, displayG, displayB};
    update.colourSpace = settings.colourSpace;
    update.applyGamutMapping = settings.applyGamutMapping;
    update.analysisSignals = analysisSignals;
    update.sourceContributions = std::move(sourceContributions);
    update.presentedContributions = std::move(presentedContributions);
    update.presentedXYZ = update.sourceAvailable ? presentedXYZ : ColourCore::XYZ{NAN, NAN, NAN};
    update.sourceTimestamp = timestamp;

    auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
    osc.updateFrameData(update);
}
#endif

void displaySpectrum(UIState& state, const ColourPresentation::SpectralContributions& contributions) {
    auto& values = state.audioSettings.spectrumContributions;
    values.assign(ColourPresentation::contributionBandCount, 0.0f);
    for (const auto& band : contributions) {
        if (band.band >= values.size()) continue;
        for (const float component : band.xyz)
            if (std::isfinite(component)) values[band.band] += std::abs(component);
    }
}

}

void sendOSCStatus(UIState& state, bool liveInput) {
#ifdef ENABLE_OSC
    const auto& recorder = state.resyneState.recorderState;
    Synesthesia::OSC::OSCFrameUpdate update;
    update.status = Synesthesia::OSC::playbackStatus(recorder.importPhase != 0, recorder.isRecording,
        recorder.audioOutput && recorder.audioOutput->getTotalFrames() > 0 ? recorder.audioOutput->isPlaying() : liveInput);
    update.sourceAvailable = false;
    update.colourSpace = state.visualSettings.colourSpace;
    update.applyGamutMapping = state.visualSettings.gamutMappingEnabled;
    update.smoothing = {state.visualSettings.smoothingEnabled, state.visualSettings.manualSmoothing,
        state.visualSettings.colourSmoothingSpeed};
    update.displayColour = {state.resyneState.displayColour[0], state.resyneState.displayColour[1], state.resyneState.displayColour[2]};
    Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance().updateFrameData(update);
#else
    static_cast<void>(state);
    static_cast<void>(liveInput);
#endif
}

void syncRecorderPresentationSettings(UIState& state) {
    auto& recorderState = state.resyneState.recorderState;
    std::lock_guard lock(recorderState.samplesMutex);
    constexpr float kNeutralGain = 1.0f;
    const bool changed =
        recorderState.importColourSpace != state.visualSettings.colourSpace ||
        recorderState.importGamutMapping != state.visualSettings.gamutMappingEnabled ||
        recorderState.importLowGain != kNeutralGain ||
        recorderState.importMidGain != kNeutralGain ||
        recorderState.importHighGain != kNeutralGain ||
        recorderState.presentationSmoothingEnabled != state.visualSettings.smoothingEnabled ||
        recorderState.presentationManualSmoothing != state.visualSettings.manualSmoothing ||
        recorderState.presentationSmoothingAmount != state.visualSettings.colourSmoothingSpeed;

    recorderState.importColourSpace = state.visualSettings.colourSpace;
    recorderState.importGamutMapping = state.visualSettings.gamutMappingEnabled;
    recorderState.importLowGain = kNeutralGain;
    recorderState.importMidGain = kNeutralGain;
    recorderState.importHighGain = kNeutralGain;
    recorderState.presentationSmoothingEnabled = state.visualSettings.smoothingEnabled;
    recorderState.presentationManualSmoothing = state.visualSettings.manualSmoothing;
    recorderState.presentationSmoothingAmount = state.visualSettings.colourSmoothingSpeed;
    recorderState.presentationResources = state.presentationResources;
    (void)changed;
}

bool hasPlaybackSession(const ReSyne::RecorderState& recorderState) {
    return recorderState.audioOutput != nullptr &&
        recorderState.audioOutput->getTotalFrames() > 0 &&
        (!recorderState.samples.empty() || recorderState.metadata.presentationData != nullptr);
}

void processPlaybackState(AudioInput& audioInput,
                          UIState& state,
                          ReSyne::RecorderState& recorderState,
                          float& currentDisplayR,
                          float& currentDisplayG,
                          float& currentDisplayB,
                          const ColourUpdateContext& ctx) {
    const auto presentation = ReSyne::recorderPresentation(recorderState);
    const double playbackPosition = ReSyne::recorderPlaybackPosition(recorderState);
    const std::size_t totalFrames = recorderState.audioOutput->getTotalFrames();
    const double normalised = totalFrames > 0 ? std::clamp(static_cast<double>(playbackPosition) / static_cast<double>(totalFrames), 0.0, 1.0) : 0.0;
    recorderState.timeline.scrubberNormalisedPosition = static_cast<float>(normalised);
    const double time = recorderState.metadata.sampleRate > 0.0f
        ? static_cast<double>(playbackPosition) / static_cast<double>(recorderState.metadata.sampleRate)
        : (presentation ? normalised * presentation->duration : 0.0);
    ImVec4 playbackColour{currentDisplayR, currentDisplayG, currentDisplayB, 1.0f};
    std::shared_ptr<const ColourPresentation::SpectralContributions> presentedContributions;
#ifdef ENABLE_OSC
    RSYNSmoothingSignals playbackSignals{};
    SpectralPresentation::Frame frame{};
    frame.sampleRate = recorderState.metadata.sampleRate;
    std::vector<float> analysisMagnitudes;
    ColourCore::FrameResult playbackColourResult{};
    bool hasPlaybackColourResult = false;
    std::shared_ptr<const ColourPresentation::SpectralContributions> sourceContributions;
    const bool needsOSC = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance().needsFrameData();
#endif
    state.presentationResponseSeconds = 0.0f;
    state.presentationResponseRange = {};
    if (presentation && presentation->analysis)
        presentedContributions = std::make_shared<const ColourPresentation::SpectralContributions>(presentation->analysis->contributionsAt(time));
    if (presentation && presentation->colours->size() > 0) {
        const auto rgb = presentation->colours->rgbAt(time, presentation->settings.colourSpace,
                                                     presentation->settings.applyGamutMapping);
        playbackColour = {rgb.r, rgb.g, rgb.b, 1.0f};
        const auto xyz = presentation->colours->xyzAt(time);
        state.presentationResponseSeconds = presentation->colours->pointAt(time).responseSeconds;
        if (presentedContributions)
            state.presentationResponseRange = ColourPresentation::responseRange(*presentedContributions);
        ctx.displayXYZ = {static_cast<float>(xyz[0]), static_cast<float>(xyz[1]), static_cast<float>(xyz[2])};
        ctx.displayXYZValid = true;
#ifdef ENABLE_OSC
        if (needsOSC && presentation->analysis && !presentation->analysis->empty()) {
            const auto index = presentation->analysis->indexAt(time);
            const auto& analysed = (*presentation->analysis)[index];
            playbackColourResult = analysed.analysis;
            hasPlaybackColourResult = true;
            playbackSignals = analysed.smoothingSignals;
            sourceContributions = analysed.contributionTargets;
            AudioColourSample sample{};
            bool hasSample = false;
            {
                std::lock_guard lock(recorderState.samplesMutex);
                if (index < recorderState.samples.size()) {
                    sample = recorderState.samples[index];
                    hasSample = true;
                }
            }
            if (hasSample) {
                frame = SpectralPresentation::SampleSequence::buildFrame(sample);
                analysisMagnitudes = SpectralPresentation::buildSharedMagnitudes(frame, buildPlaybackPresentationSettings(state, recorderState));
            }
        }
#endif
    }

    displaySpectrum(state, presentedContributions ? *presentedContributions : ColourPresentation::SpectralContributions{});

    currentDisplayR = playbackColour.x;
    currentDisplayG = playbackColour.y;
    currentDisplayB = playbackColour.z;
    ColourPresentation::applyOutputPrecision(
        currentDisplayR,
        currentDisplayG,
        currentDisplayB);
    ctx.clearColour[0] = currentDisplayR;
    ctx.clearColour[1] = currentDisplayG;
    ctx.clearColour[2] = currentDisplayB;
    state.resyneState.displayColour[0] = currentDisplayR;
    state.resyneState.displayColour[1] = currentDisplayG;
    state.resyneState.displayColour[2] = currentDisplayB;

#ifdef ENABLE_OSC
    if (needsOSC && hasPlaybackColourResult) {
        const Synesthesia::OSC::OSCAnalysisSignals analysisSignals{
            playbackSignals.spectralFlux, playbackSignals.onsetDetected};
        sendOSCFrame(state, presentation->settings,
            analysisMagnitudes,
            frame,
            playbackColourResult,
            currentDisplayR,
            currentDisplayG,
            currentDisplayB,
            analysisSignals,
            ctx.displayXYZ, time, state.presentationResponseSeconds, state.presentationResponseRange,
            presentation->sourceGeneration * 2 + 1, sourceContributions, presentedContributions, recorderState.metadata.fftSize);
    } else if (needsOSC) sendOSCStatus(state, false);
#endif

    ReSyne::updateFromFFT(
        state.resyneState,
        audioInput.getAudioProcessor(),
        audioInput.getSampleRate(),
        currentDisplayR,
        currentDisplayG,
        currentDisplayB);
}

void processLiveAudioState(AudioInput& audioInput,
                           UIState& state,
                           ReSyne::RecorderState& recorderState,
                           float& currentDisplayR,
                           float& currentDisplayG,
                           float& currentDisplayB,
                           const ColourUpdateContext& ctx) {
    RSYNPresentationSettings settings;
    settings.colourSpace = state.visualSettings.colourSpace;
    settings.applyGamutMapping = state.visualSettings.gamutMappingEnabled;
    settings.lowGain = state.audioSettings.lowGain;
    settings.midGain = state.audioSettings.midGain;
    settings.highGain = state.audioSettings.highGain;
    settings.smoothingEnabled = state.visualSettings.smoothingEnabled;
    settings.manualSmoothing = state.visualSettings.manualSmoothing;
    settings.smoothingAmount = state.visualSettings.colourSmoothingSpeed;
    audioInput.getAudioProcessor().setPresentationSettings(settings);
    const auto live = audioInput.getAudioProcessor().getPresentation();
    if (!live) {
        sendOSCStatus(state, true);
        return;
    }
    if (!recorderState.isRecording) audioInput.getAudioProcessor().markPresentationDisplayed(*live);
    auto displayedContributions = live->frame.presentedContributions;
#ifdef ENABLE_OSC
    double displayedTimestamp = live->frame.timestamp;
    auto displayedSettings = live->settings;
    uint64_t displayedSourceId = static_cast<uint64_t>(state.deviceState.selectedDeviceIndex + 1) * 2 |
        (static_cast<uint64_t>(live->selectedChannel + 1) << 32);
    const auto* colourResult = &live->frame.analysis;
    const auto* analysisMagnitudes = &live->prepared.analysisMagnitudes;
    auto liveSignals = live->frame.smoothingSignals;
    const auto* sourceFrame = live->source.get();
    auto sourceContributions = live->frame.contributionTargets;
    std::shared_ptr<const ReSyne::PresentationSnapshot> displayedRecording;
    SpectralPresentation::Frame recordedSource;
    std::vector<float> recordedMagnitudes;
#endif
    state.presentationResponseSeconds = live->frame.responseSeconds;
    state.presentationResponseRange = live->frame.presentedContributions ?
        ColourPresentation::responseRange(*live->frame.presentedContributions) : ColourPresentation::ResponseRange{};
    const auto& liveXYZ = live->frame.presentedXYZ;
    ctx.displayXYZ = {liveXYZ[0], liveXYZ[1], liveXYZ[2]};
    ctx.displayXYZValid = true;
    const auto rgb = ColourPresentation::projectXYZ({static_cast<double>(ctx.displayXYZ.X), static_cast<double>(ctx.displayXYZ.Y), static_cast<double>(ctx.displayXYZ.Z)},
        state.visualSettings.colourSpace, state.visualSettings.gamutMappingEnabled);
    currentDisplayR = rgb.r; currentDisplayG = rgb.g; currentDisplayB = rgb.b;
    if (recorderState.isRecording) {
        const auto presentation = ReSyne::recorderPresentation(recorderState);
        if (presentation && presentation->colours->size() > 0) {
            const double time = presentation->colours->endTime();
#ifdef ENABLE_OSC
            sourceFrame = nullptr;
            displayedTimestamp = time;
            displayedSettings = presentation->settings;
            displayedSourceId = presentation->sourceGeneration * 2 + 1;
#endif
            const auto xyz = presentation->colours->xyzAt(time);
            const auto recordedRgb = presentation->colours->rgbAt(time, state.visualSettings.colourSpace,
                state.visualSettings.gamutMappingEnabled);
            currentDisplayR = recordedRgb.r; currentDisplayG = recordedRgb.g; currentDisplayB = recordedRgb.b;
            ctx.displayXYZ = {static_cast<float>(xyz[0]), static_cast<float>(xyz[1]), static_cast<float>(xyz[2])};
            ctx.displayXYZValid = true;
            state.presentationResponseSeconds = presentation->colours->pointAt(time).responseSeconds;
            if (presentation->analysis && !presentation->analysis->empty()) {
                const auto& recordedFrame = (*presentation->analysis)[presentation->analysis->indexAt(time)];
                displayedContributions = recordedFrame.presentedContributions;
#ifdef ENABLE_OSC
                displayedRecording = presentation;
                colourResult = &recordedFrame.analysis;
                liveSignals = recordedFrame.smoothingSignals;
                sourceContributions = recordedFrame.contributionTargets;
                sourceFrame = nullptr;
                if (Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance().needsFrameData()) {
                    AudioColourSample source;
                    bool available = false;
                    {
                        std::lock_guard lock(recorderState.samplesMutex);
                        const auto index = presentation->analysis->indexAt(time);
                        if (presentation->sourceGeneration == recorderState.presentationSourceGeneration && index < recorderState.samples.size()) {
                            source = recorderState.samples[index];
                            available = true;
                        }
                    }
                    if (available) {
                        recordedSource = SpectralPresentation::SampleSequence::buildFrame(source);
                        SpectralPresentation::Settings sourceSettings;
                        sourceSettings.lowGain = displayedSettings.lowGain;
                        sourceSettings.midGain = displayedSettings.midGain;
                        sourceSettings.highGain = displayedSettings.highGain;
                        sourceSettings.colourSpace = displayedSettings.colourSpace;
                        sourceSettings.applyGamutMapping = displayedSettings.applyGamutMapping;
                        recordedMagnitudes = SpectralPresentation::buildSharedMagnitudes(recordedSource, sourceSettings);
                        analysisMagnitudes = &recordedMagnitudes;
                        sourceFrame = &recordedSource;
                    }
                }
#endif
                state.presentationResponseRange = displayedContributions ? ColourPresentation::responseRange(*displayedContributions) :
                    ColourPresentation::ResponseRange{};
            }
        }
    }

    ColourPresentation::applyOutputPrecision(
        currentDisplayR,
        currentDisplayG,
        currentDisplayB);
    ctx.clearColour[0] = currentDisplayR;
    ctx.clearColour[1] = currentDisplayG;
    ctx.clearColour[2] = currentDisplayB;

#ifdef ENABLE_OSC
    const Synesthesia::OSC::OSCAnalysisSignals analysisSignals{
        liveSignals.spectralFlux, liveSignals.onsetDetected};
    if (sourceFrame && Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance().needsFrameData()) {
        sendOSCFrame(state, displayedSettings,
            *analysisMagnitudes,
            *sourceFrame,
            *colourResult,
            currentDisplayR,
            currentDisplayG,
            currentDisplayB,
            analysisSignals,
            ctx.displayXYZ, displayedTimestamp, state.presentationResponseSeconds,
            state.presentationResponseRange, displayedSourceId, sourceContributions, displayedContributions);
    } else if (Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance().needsFrameData()) {
        sendOSCStatus(state, true);
    }
#endif

    ReSyne::updateFromFFT(
        state.resyneState,
        audioInput.getAudioProcessor(),
        audioInput.getSampleRate(),
        currentDisplayR,
        currentDisplayG,
        currentDisplayB);

    displaySpectrum(state, displayedContributions ? *displayedContributions : ColourPresentation::SpectralContributions{});
}

}

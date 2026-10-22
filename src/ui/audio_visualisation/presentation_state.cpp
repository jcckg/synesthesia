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
AudioProcessor::SpectralData liveSpectralScratch;


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

#ifdef ENABLE_OSC
void sendOSCFrame(const std::vector<float>& magnitudes,
                  const SpectralPresentation::Frame& frame,
                  const ColourCore::FrameResult& colourResult,
                  const float displayR,
                  const float displayG,
                  const float displayB,
                  const Synesthesia::OSC::OSCAnalysisSignals& analysisSignals,
                  const ColourCore::XYZ& presentedXYZ, double timestamp, int fftSize = 0) {
    Synesthesia::OSC::OSCFrameUpdate update{};
    update.magnitudes = std::span<const float>(magnitudes.data(), magnitudes.size());
    update.phases = std::span<const float>(frame.phases.data(), frame.phases.size());
    update.frequencies = frame.frequencies;
    update.fftSize = fftSize;
    update.sampleRate = frame.sampleRate;
    update.colourResult = colourResult;
    update.displayColour = ColourCore::RGB{displayR, displayG, displayB};
    update.analysisSignals = analysisSignals;
    update.presentedXYZ = presentedXYZ;
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
    const double playbackPosition = recorderState.audioOutput->getPresentationPosition();
    const std::size_t totalFrames = recorderState.audioOutput->getTotalFrames();
    const double normalised = totalFrames > 0 ? std::clamp(static_cast<double>(playbackPosition) / static_cast<double>(totalFrames), 0.0, 1.0) : 0.0;
    recorderState.timeline.scrubberNormalisedPosition = static_cast<float>(normalised);
    const double time = recorderState.metadata.sampleRate > 0.0f
        ? static_cast<double>(playbackPosition) / static_cast<double>(recorderState.metadata.sampleRate)
        : (presentation ? normalised * presentation->duration : 0.0);
    ImVec4 playbackColour{currentDisplayR, currentDisplayG, currentDisplayB, 1.0f};
    RSYNSmoothingSignals playbackSignals{};
    SpectralPresentation::Frame frame{};
    frame.sampleRate = recorderState.metadata.sampleRate;
    std::vector<float> analysisMagnitudes;
    ColourCore::FrameResult playbackColourResult{};
    bool hasPlaybackColourResult = false;
    if (presentation && presentation->colours->size() > 0) {
        const auto rgb = presentation->colours->rgbAt(time, presentation->settings.colourSpace,
                                                     presentation->settings.applyGamutMapping);
        playbackColour = {rgb.r, rgb.g, rgb.b, 1.0f};
        const auto xyz = presentation->colours->xyzAt(time);
        ctx.displayXYZ = {static_cast<float>(xyz[0]), static_cast<float>(xyz[1]), static_cast<float>(xyz[2])};
        ctx.displayXYZValid = true;
        if (presentation->analysis && !presentation->analysis->empty()) {
            const auto index = presentation->analysis->indexAt(time);
            const auto& analysed = (*presentation->analysis)[index];
            playbackColourResult = analysed.analysis;
            hasPlaybackColourResult = true;
            playbackSignals = analysed.smoothingSignals;
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
    }

    displaySpectrum(state, presentation && presentation->analysis
        ? presentation->analysis->contributionsAt(time) : ColourPresentation::SpectralContributions{});

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
    if (hasPlaybackColourResult) {
        const Synesthesia::OSC::OSCAnalysisSignals analysisSignals{
            playbackSignals.spectralFlux, playbackSignals.onsetDetected};
        sendOSCFrame(
            analysisMagnitudes,
            frame,
            playbackColourResult,
            currentDisplayR,
            currentDisplayG,
            currentDisplayB,
            analysisSignals,
            ctx.displayXYZ, time, recorderState.metadata.fftSize);
    }
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
    audioInput.copySpectralData(liveSpectralScratch);
    const auto& spectralData = liveSpectralScratch;
    const auto& live = spectralData.presentation;
    if (!live) return;
    auto displayedContributions = live->frame.presentedContributions;
    double displayedTimestamp = live->frame.timestamp;
    const auto& colourResult = live->frame.analysis;
    const auto& analysisMagnitudes = live->prepared.analysisMagnitudes;
    const auto& liveSignals = live->frame.smoothingSignals;
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
            displayedTimestamp = time;
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
    const auto frame = SpectralPresentation::mixChannels(spectralData.magnitudes, spectralData.phases, {},
        static_cast<std::uint32_t>(spectralData.magnitudes.size()), spectralData.sampleRate);
    sendOSCFrame(
        analysisMagnitudes,
        frame,
        colourResult,
        currentDisplayR,
        currentDisplayG,
        currentDisplayB,
        analysisSignals,
        ctx.displayXYZ, displayedTimestamp);
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

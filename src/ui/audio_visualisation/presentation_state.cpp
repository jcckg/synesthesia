#include "ui/audio_visualisation/presentation_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <vector>

#include "audio/analysis/presentation/sample_sequence.h"
#include "audio/analysis/presentation/spectral_presentation.h"
#include "colour/colour_core.h"
#include "colour/colour_presentation.h"
#include "resyne/controller/controller.h"
#include "resyne/recorder/colour_cache_utils.h"
#include "resyne/ui/timeline/timeline_gradient.h"
#include "ui/smoothing/smoothing_features.h"

#ifdef ENABLE_OSC
#include "synesthesia_osc_integration.h"
#endif

namespace UI::AudioVisualisation {

namespace {
::UI::Smoothing::MagnitudeHistory playbackSmoothingState;
AudioProcessor::SpectralData liveSpectralScratch;

struct LivePhaseState {
    SpectralPresentation::Frame previousFrame;
    uint64_t previousFrameCounter = 0;
    bool hasPreviousFrame = false;
};

LivePhaseState livePhaseState;

struct LiveEQState {
    float lowGain = 1.0f;
    float midGain = 1.0f;
    float highGain = 1.0f;
    bool initialised = false;
};

LiveEQState liveEQState;

constexpr float kSilenceMagnitudeThreshold = 1e-5f;

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

SpectralPresentation::Settings buildLivePresentationSettings(const UIState& state) {
    return buildPresentationSettings(
        state,
        state.audioSettings.lowGain,
        state.audioSettings.midGain,
        state.audioSettings.highGain);
}

SpectralPresentation::Settings buildPlaybackPresentationSettings(const UIState& state,
                                                                 const ReSyne::RecorderState& recorderState) {
    return buildPresentationSettings(
        state,
        recorderState.importLowGain,
        recorderState.importMidGain,
        recorderState.importHighGain);
}

bool spectrumIsSilent(const std::vector<float>& magnitudes) {
    for (const float magnitude : magnitudes) {
        if (!std::isfinite(magnitude)) {
            continue;
        }
        if (std::fabs(magnitude) > kSilenceMagnitudeThreshold) {
            return false;
        }
    }
    return true;
}

void resetAnalyserState(UIState& state,
                        const size_t binCount,
                        const size_t numChannels = 1) {
    if (state.audioSettings.smoothedMagnitudes.size() != numChannels) {
        state.audioSettings.smoothedMagnitudes.resize(numChannels);
    }
    for (auto& channelMagnitudes : state.audioSettings.smoothedMagnitudes) {
        if (channelMagnitudes.size() != binCount) {
            channelMagnitudes.assign(binCount, 0.0f);
        } else {
            std::fill(channelMagnitudes.begin(), channelMagnitudes.end(), 0.0f);
        }
    }
    state.spectrumAnalyser.resetTemporalBuffers();
}

void applyColourSmoothing(const ColourCore::FrameResult& targetColour,
                          const SpectralPresentation::Settings& presentationSettings,
                          float& outR,
                          float& outG,
                          float& outB,
                          const ColourUpdateContext& ctx,
                          const SmoothingSignalFeatures* signalFeatures) {
    if (ctx.smoothingEnabled) {
        float targetL = 0.0f;
        float targetA = 0.0f;
        float targetB = 0.0f;
        ColourCore::XYZtoOklab(targetColour.X, targetColour.Y, targetColour.Z, targetL, targetA, targetB);
        ctx.colourSmoother.setTargetOklab(targetL, targetA, targetB);
        if (!ctx.manualSmoothing && signalFeatures != nullptr) {
            ctx.colourSmoother.update(ctx.deltaTime * UIConstants::COLOUR_SMOOTH_UPDATE_FACTOR, *signalFeatures);
        } else {
            ctx.colourSmoother.update(ctx.deltaTime * UIConstants::COLOUR_SMOOTH_UPDATE_FACTOR);
        }

        float smoothedL = 0.0f;
        float smoothedA = 0.0f;
        float smoothedB = 0.0f;
        ctx.colourSmoother.getCurrentOklab(smoothedL, smoothedA, smoothedB);
        float smoothedX = 0.0f;
        float smoothedY = 0.0f;
        float smoothedZ = 0.0f;
        ColourCore::OklabtoXYZ(smoothedL, smoothedA, smoothedB, smoothedX, smoothedY, smoothedZ);
        const auto smoothedRGB = SpectralPresentation::displayRGBFromXYZ(
            smoothedX,
            smoothedY,
            smoothedZ,
            presentationSettings);

        if (ctx.activeView == UIState::View::ReSyne) {
            outR = smoothedRGB[0];
            outG = smoothedRGB[1];
            outB = smoothedRGB[2];
        } else {
            ctx.clearColour[0] = smoothedRGB[0];
            ctx.clearColour[1] = smoothedRGB[1];
            ctx.clearColour[2] = smoothedRGB[2];
            outR = ctx.clearColour[0];
            outG = ctx.clearColour[1];
            outB = ctx.clearColour[2];
        }
    } else {
        outR = targetColour.r;
        outG = targetColour.g;
        outB = targetColour.b;
        if (ctx.activeView != UIState::View::ReSyne) {
            ctx.clearColour[0] = targetColour.r;
            ctx.clearColour[1] = targetColour.g;
            ctx.clearColour[2] = targetColour.b;
        }
    }
}

float resolveSpectrumHistoryFactor(const UIState& state,
                                   const SmoothingSignalFeatures* features) {
    const float baseSmoothing = std::clamp(state.audioSettings.spectrumSmoothingFactor, 0.0f, 1.0f);
    if (state.visualSettings.manualSmoothing || features == nullptr) {
        return baseSmoothing;
    }
    return resolveAdaptiveSmoothingAmount(baseSmoothing, *features);
}

void applyLiveEQIfNeeded(AudioInput& audioInput, const UIState& state) {
    const bool gainsChanged = !liveEQState.initialised ||
        liveEQState.lowGain != state.audioSettings.lowGain ||
        liveEQState.midGain != state.audioSettings.midGain ||
        liveEQState.highGain != state.audioSettings.highGain;
    if (!gainsChanged) {
        return;
    }

    audioInput.setEQGains(
        state.audioSettings.lowGain,
        state.audioSettings.midGain,
        state.audioSettings.highGain);
    liveEQState.lowGain = state.audioSettings.lowGain;
    liveEQState.midGain = state.audioSettings.midGain;
    liveEQState.highGain = state.audioSettings.highGain;
    liveEQState.initialised = true;
}

}

void syncRecorderPresentationSettings(UIState& state) {
    auto& recorderState = state.resyneState.recorderState;
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
        !recorderState.samples.empty();
}

void processPlaybackState(AudioInput& audioInput,
                          UIState& state,
                          ReSyne::RecorderState& recorderState,
                          float& currentDisplayR,
                          float& currentDisplayG,
                          float& currentDisplayB,
                          const ColourUpdateContext& ctx) {
    size_t playbackPosition = recorderState.audioOutput->getPlaybackPosition();

    if (!recorderState.samples.empty()) {
        const size_t totalFrames = recorderState.audioOutput->getTotalFrames();
        if (totalFrames > 0) {
            const float audioNormalised = static_cast<float>(playbackPosition) / static_cast<float>(totalFrames);
            const size_t spectralFrame = static_cast<size_t>(
                std::clamp(audioNormalised, 0.0f, 1.0f) * static_cast<float>(recorderState.samples.size()));
            const size_t clampedSpectralFrame = std::min(spectralFrame, recorderState.samples.size() - 1);
            recorderState.timeline.scrubberNormalisedPosition = recorderState.samples.size() == 1
                ? 0.0f
                : static_cast<float>(clampedSpectralFrame) /
                    static_cast<float>(recorderState.samples.size() - 1);
        }
    }

    ImVec4 playbackColour = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    SmoothingSignalFeatures playbackSignalFeatures{};
    bool playbackSignalFeaturesValid = false;
    SpectralPresentation::Frame frame{};
    std::vector<float> visualiserMagnitudes;
    ColourCore::FrameResult playbackColourResult{};
    bool hasPlaybackColourResult = false;

    if (!recorderState.samples.empty()) {
        const float scrubberPos = std::clamp(recorderState.timeline.scrubberNormalisedPosition, 0.0f, 1.0f);
        const auto presentationSettings = buildPlaybackPresentationSettings(state, recorderState);

        std::lock_guard<std::mutex> lock(recorderState.samplesMutex);
        const float position = scrubberPos * (static_cast<float>(recorderState.samples.size()) - 1.0f);
        const size_t sampleIndex = static_cast<size_t>(position);
        const size_t clampedIndex = std::min(sampleIndex, recorderState.samples.size() - 1);
        auto colourSettings = ReSyne::RecorderColourCache::currentSettings(recorderState);
        colourSettings.smoothingEnabled = false;
        colourSettings.manualSmoothing = false;
        colourSettings.smoothingAmount = 0.0f;

        const auto makeSample = [&](const size_t index) {
            const AudioColourSample* previousSample =
                index > 0 ? &recorderState.samples[index - 1] : nullptr;
            const auto entry = ReSyne::RecorderColourCache::computeSampleColour(
                recorderState.samples[index],
                colourSettings,
                previousSample);
            ReSyne::Timeline::TimelineSample sample{};
            sample.timestamp = recorderState.samples[index].timestamp;
            sample.colour = entry.rgb;
            sample.labL = entry.labL;
            sample.labA = entry.labA;
            sample.labB = entry.labB;
            return sample;
        };

        if (recorderState.samples.size() == 1) {
            playbackColour = makeSample(0).colour;
        } else {
            const float t = position - static_cast<float>(sampleIndex);
            playbackColour = ReSyne::Timeline::Gradient::interpolateColour(
                makeSample(sampleIndex),
                makeSample(std::min(sampleIndex + 1, recorderState.samples.size() - 1)),
                t,
                recorderState.importColourSpace,
                recorderState.importGamutMapping);
        }

        const auto& currentSample = recorderState.samples[clampedIndex];
        const AudioColourSample* previousSample =
            clampedIndex > 0 ? &recorderState.samples[clampedIndex - 1] : nullptr;
        frame = SpectralPresentation::SampleSequence::buildFrame(currentSample);

        if (!frame.magnitudes.empty()) {
            const auto preparedFrame = SpectralPresentation::SampleSequence::prepareSampleFrame(
                currentSample,
                presentationSettings,
                previousSample,
                ctx.deltaTime);
            visualiserMagnitudes = preparedFrame.visualiserMagnitudes;
            const auto& colourResult = preparedFrame.colourResult;
            playbackColourResult = colourResult;
            hasPlaybackColourResult = true;

            playbackSignalFeatures = ::UI::Smoothing::buildSignalFeatures(colourResult);
            playbackSignalFeaturesValid = true;
        }
    }

    if (visualiserMagnitudes.empty()) {
        resetAnalyserState(state, 0);
    } else {
        if (playbackSignalFeaturesValid) {
            ::UI::Smoothing::updateFluxHistory(
                visualiserMagnitudes,
                playbackSmoothingState,
                playbackSignalFeatures);
        }

        const size_t channelIndex = 0;
        if (spectrumIsSilent(visualiserMagnitudes)) {
            resetAnalyserState(state, visualiserMagnitudes.size(), 1);
        } else {
            if (state.audioSettings.smoothedMagnitudes.empty()) {
                state.audioSettings.smoothedMagnitudes.resize(1);
            }
            if (state.audioSettings.smoothedMagnitudes[channelIndex].size() != visualiserMagnitudes.size()) {
                state.audioSettings.smoothedMagnitudes[channelIndex].assign(visualiserMagnitudes.size(), 0.0f);
            }

            const float smoothing = resolveSpectrumHistoryFactor(
                state,
                playbackSignalFeaturesValid ? &playbackSignalFeatures : nullptr);
            const float newContribution = 1.0f - smoothing;
            const float historyContribution = smoothing;

            for (size_t index = 0; index < visualiserMagnitudes.size(); ++index) {
                state.audioSettings.smoothedMagnitudes[channelIndex][index] =
                    newContribution * visualiserMagnitudes[index] +
                    historyContribution * state.audioSettings.smoothedMagnitudes[channelIndex][index];
            }
        }
    }

    if (hasPlaybackColourResult) {
        applyColourSmoothing(
            playbackColourResult,
            buildPlaybackPresentationSettings(state, recorderState),
            currentDisplayR,
            currentDisplayG,
            currentDisplayB,
            ctx,
            playbackSignalFeaturesValid ? &playbackSignalFeatures : nullptr);
    } else {
        currentDisplayR = playbackColour.x;
        currentDisplayG = playbackColour.y;
        currentDisplayB = playbackColour.z;
    }
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
    if (hasPlaybackColourResult && !visualiserMagnitudes.empty()) {
        auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
        osc.updateColourData(
            visualiserMagnitudes,
            frame.phases,
            playbackColourResult.dominantFrequency,
            frame.sampleRate,
            currentDisplayR,
            currentDisplayG,
            currentDisplayB);
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
    (void)recorderState;
    const auto presentationSettings = buildLivePresentationSettings(state);
    audioInput.copySpectralData(liveSpectralScratch);
    const auto& spectralData = liveSpectralScratch;
    const size_t numChannels = spectralData.magnitudes.size();

    applyLiveEQIfNeeded(audioInput, state);

    const SpectralPresentation::Frame frame = SpectralPresentation::mixChannels(
        spectralData.magnitudes,
        spectralData.phases,
        {},
        static_cast<std::uint32_t>(numChannels),
        spectralData.sampleRate > 0.0f ? spectralData.sampleRate : audioInput.getSampleRate());
    const float liveLoudnessDb = spectralData.momentaryLoudnessLUFS;
    const uint64_t frameCounter = spectralData.frameCounter;
    const bool hasNewFrame = !livePhaseState.hasPreviousFrame || frameCounter != livePhaseState.previousFrameCounter;
    const auto preparedFrame = SpectralPresentation::prepareFrame(
        frame,
        presentationSettings,
        liveLoudnessDb,
        hasNewFrame && livePhaseState.hasPreviousFrame ? &livePhaseState.previousFrame : nullptr,
        spectralData.sampleRate > 0.0f
            ? static_cast<float>(spectralData.hopSize) / spectralData.sampleRate
            : ctx.deltaTime);
    if (hasNewFrame) {
        livePhaseState.previousFrame = frame;
        livePhaseState.previousFrameCounter = frameCounter;
        livePhaseState.hasPreviousFrame = true;
    }
    const std::vector<float>& visualiserMagnitudes = preparedFrame.visualiserMagnitudes;
    const bool silentMagnitudeFrame = spectrumIsSilent(visualiserMagnitudes);
    const auto& colourResult = preparedFrame.colourResult;
    SmoothingSignalFeatures liveFeatures{};
    bool liveFeaturesValid = false;

    const float displayR = std::clamp(colourResult.r, 0.0f, 1.0f);
    const float displayG = std::clamp(colourResult.g, 0.0f, 1.0f);
    const float displayB = std::clamp(colourResult.b, 0.0f, 1.0f);

    if (!std::isfinite(ctx.clearColour[0]) || !std::isfinite(ctx.clearColour[1]) || !std::isfinite(ctx.clearColour[2])) {
        ctx.clearColour[0] = 0.1f;
        ctx.clearColour[1] = 0.1f;
        ctx.clearColour[2] = 0.1f;
    }

    if (std::isfinite(displayR) && std::isfinite(displayG) && std::isfinite(displayB)) {
        liveFeatures = ::UI::Smoothing::buildSignalFeatures(colourResult);
        liveFeatures.onsetDetected = spectralData.onsetDetected;
        liveFeatures.spectralFlux = spectralData.spectralFlux;
        liveFeaturesValid = true;

        applyColourSmoothing(
            colourResult,
            presentationSettings,
            currentDisplayR,
            currentDisplayG,
            currentDisplayB,
            ctx,
            &liveFeatures);
    }

    ColourPresentation::applyOutputPrecision(
        currentDisplayR,
        currentDisplayG,
        currentDisplayB);
    ctx.clearColour[0] = currentDisplayR;
    ctx.clearColour[1] = currentDisplayG;
    ctx.clearColour[2] = currentDisplayB;

#ifdef ENABLE_OSC
    auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
    osc.updateColourData(
        visualiserMagnitudes,
        frame.phases,
        colourResult.dominantFrequency,
        frame.sampleRate,
        currentDisplayR,
        currentDisplayG,
        currentDisplayB);
#endif

    ReSyne::updateFromFFT(
        state.resyneState,
        audioInput.getAudioProcessor(),
        audioInput.getSampleRate(),
        currentDisplayR,
        currentDisplayG,
        currentDisplayB);

    const size_t channelIndex = 0;
    if (silentMagnitudeFrame || visualiserMagnitudes.empty()) {
        resetAnalyserState(state, visualiserMagnitudes.size(), 1);
    } else {
        if (state.audioSettings.smoothedMagnitudes.empty()) {
            state.audioSettings.smoothedMagnitudes.resize(1);
        }
        if (state.audioSettings.smoothedMagnitudes[channelIndex].size() != visualiserMagnitudes.size()) {
            state.audioSettings.smoothedMagnitudes[channelIndex].assign(visualiserMagnitudes.size(), 0.0f);
        }

        const float smoothing = resolveSpectrumHistoryFactor(
            state,
            liveFeaturesValid ? &liveFeatures : nullptr);
        const float newContribution = 1.0f - smoothing;
        const float historyContribution = smoothing;

        for (size_t index = 0; index < visualiserMagnitudes.size(); ++index) {
            state.audioSettings.smoothedMagnitudes[channelIndex][index] =
                newContribution * visualiserMagnitudes[index] +
                historyContribution * state.audioSettings.smoothedMagnitudes[channelIndex][index];
        }
    }
}

}

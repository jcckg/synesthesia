#include "resyne/ui/recorder/shared_components.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "imgui.h"

#include "audio/analysis/presentation/spectral_presentation.h"
#include "resyne/recorder/colour_cache_utils.h"
#include "ui/smoothing/smoothing.h"

namespace ReSyne::UI {

namespace {

constexpr auto kInteractivePreviewRebuildDebounce = std::chrono::milliseconds(120);

bool previewSettingsMatch(const RecorderState& state,
                          const RecorderColourCache::CacheSettings& settings,
                          const size_t maxSamples,
                          const size_t sourceCount,
                          const bool usePreview) {
    return !state.timelinePreviewCacheDirty &&
        state.timelinePreviewCacheMaxSamples == maxSamples &&
        state.timelinePreviewCacheSourceCount == sourceCount &&
        state.timelinePreviewCacheUsesPreviewSamples == usePreview &&
        state.timelinePreviewCacheGamma == settings.gamma &&
        state.timelinePreviewCacheColourSpace == settings.colourSpace &&
        state.timelinePreviewCacheGamutMapping == settings.gamutMapping &&
        state.timelinePreviewCacheLowGain == settings.lowGain &&
        state.timelinePreviewCacheMidGain == settings.midGain &&
        state.timelinePreviewCacheHighGain == settings.highGain &&
        state.timelinePreviewCacheSmoothingEnabled == settings.smoothingEnabled &&
        state.timelinePreviewCacheManualSmoothing == settings.manualSmoothing &&
        state.timelinePreviewCacheSmoothingAmount == settings.smoothingAmount;
}

bool shouldThrottlePreviewRebuild(const RecorderState& state) {
    if (state.timelinePreviewCache.empty()) {
        return false;
    }

    using namespace std::chrono;
    const auto now = steady_clock::now();
    const auto elapsed = duration_cast<milliseconds>(now - state.timelinePreviewCacheLastBuildTime);
    return elapsed.count() < 50;
}

bool shouldDeferInteractivePreviewRebuild(const RecorderState& state) {
    if (!state.presentationSettingsSettling || state.timelinePreviewCache.empty()) {
        return false;
    }

    using namespace std::chrono;
    const auto elapsed = duration_cast<milliseconds>(
        steady_clock::now() - state.presentationSettingsLastChangedTime);
    return elapsed < kInteractivePreviewRebuildDebounce;
}

void storePreviewSettings(RecorderState& state,
                          const RecorderColourCache::CacheSettings& settings,
                          const size_t maxSamples,
                          const size_t sourceCount,
                          const bool usePreview) {
    state.timelinePreviewCacheMaxSamples = maxSamples;
    state.timelinePreviewCacheSourceCount = sourceCount;
    state.timelinePreviewCacheUsesPreviewSamples = usePreview;
    state.timelinePreviewCacheGamma = settings.gamma;
    state.timelinePreviewCacheColourSpace = settings.colourSpace;
    state.timelinePreviewCacheGamutMapping = settings.gamutMapping;
    state.timelinePreviewCacheLowGain = settings.lowGain;
    state.timelinePreviewCacheMidGain = settings.midGain;
    state.timelinePreviewCacheHighGain = settings.highGain;
    state.timelinePreviewCacheSmoothingEnabled = settings.smoothingEnabled;
    state.timelinePreviewCacheManualSmoothing = settings.manualSmoothing;
    state.timelinePreviewCacheSmoothingAmount = settings.smoothingAmount;
    state.timelinePreviewCacheDirty = false;
    state.timelinePreviewCacheLastBuildTime = std::chrono::steady_clock::now();
}

SpectralPresentation::Settings buildPresentationSettings(const RecorderColourCache::CacheSettings& settings) {
    SpectralPresentation::Settings presentation{};
    presentation.lowGain = settings.lowGain;
    presentation.midGain = settings.midGain;
    presentation.highGain = settings.highGain;
    presentation.gamma = settings.gamma;
    presentation.colourSpace = settings.colourSpace;
    presentation.applyGamutMapping = settings.gamutMapping;
    return presentation;
}

Timeline::TimelineSample buildTimelineSample(const AudioColourSample& sample,
                                             const AudioColourSample* previousSample,
                                             const RecorderColourCache::CacheSettings& settings) {
    const auto entry = RecorderColourCache::computeSampleColour(sample, settings, previousSample);
    Timeline::TimelineSample output{};
    output.timestamp = sample.timestamp;
    output.colour = entry.rgb;
    output.labL = entry.labL;
    output.labA = entry.labA;
    output.labB = entry.labB;
    return output;
}

Timeline::TimelineSample buildTimelineSampleFromXYZ(const double timestamp,
                                                    const float X,
                                                    const float Y,
                                                    const float Z,
                                                    const RecorderColourCache::CacheSettings& settings) {
    Timeline::TimelineSample output{};
    output.timestamp = timestamp;
    const auto rgb = SpectralPresentation::displayRGBFromXYZ(
        X,
        Y,
        Z,
        buildPresentationSettings(settings));
    output.colour = ImVec4(
        std::clamp(rgb[0], 0.0f, 1.0f),
        std::clamp(rgb[1], 0.0f, 1.0f),
        std::clamp(rgb[2], 0.0f, 1.0f),
        1.0f);
    ColourMapper::XYZtoLab(X, Y, Z, output.labL, output.labA, output.labB);
    return output;
}

void populateSpectralNorms(const ColourMapper::ColourResult& result,
                           SmoothingSignalFeatures& features) {
    constexpr float minCentroid = 100.0f;
    constexpr float minRolloff = 20.0f;
    constexpr float rolloffLogMin = 4.32f;
    constexpr float rolloffLogMax = 14.29f;
    constexpr float crestLogScale = 4.0f;

    const float centroid = std::max(result.spectralCentroid, minCentroid);
    features.spectralSpreadNorm = std::clamp(result.spectralSpread / centroid * 0.5f, 0.0f, 1.0f);
    const float rolloffLog = std::log2(std::max(result.spectralRolloff, minRolloff));
    features.spectralRolloffNorm = std::clamp((rolloffLog - rolloffLogMin) / (rolloffLogMax - rolloffLogMin), 0.0f, 1.0f);
    features.spectralCrestNorm = std::clamp(std::log2(std::max(result.spectralCrestFactor, 1.0f)) / crestLogScale, 0.0f, 1.0f);
}

void applyPreviewSmoothing(std::vector<Timeline::TimelineSample>& previewData,
                           const std::vector<AudioColourSample>& sourceSamples,
                           const std::vector<size_t>& sampledIndices,
                           const RecorderColourCache::CacheSettings& settings) {
    if (!settings.smoothingEnabled || previewData.size() < 2) {
        return;
    }

    SpringSmoother smoother(8.0f, 1.0f, 0.3f);
    smoother.setSmoothingAmount(settings.smoothingAmount);
    if (!sampledIndices.empty() && sampledIndices.front() < sourceSamples.size()) {
        const auto& initialSample = sourceSamples[sampledIndices.front()];
        const auto initialFrame = SpectralPresentation::mixChannels(
            initialSample.magnitudes,
            initialSample.phases,
            initialSample.frequencies,
            initialSample.channels,
            initialSample.sampleRate);
        const float initialLoudness = std::isfinite(initialSample.loudnessLUFS)
            ? initialSample.loudnessLUFS
            : ColourMapper::LOUDNESS_DB_UNSPECIFIED;
        const auto initialPreparedFrame = SpectralPresentation::prepareFrame(
            initialFrame,
            buildPresentationSettings(settings),
            initialLoudness);
        float initialL = 0.0f;
        float initialA = 0.0f;
        float initialB = 0.0f;
        ColourMapper::XYZtoOklab(
            initialPreparedFrame.colourResult.X,
            initialPreparedFrame.colourResult.Y,
            initialPreparedFrame.colourResult.Z,
            initialL,
            initialA,
            initialB);
        smoother.resetOklab(initialL, initialA, initialB);
    } else {
        smoother.reset(previewData.front().colour.x, previewData.front().colour.y, previewData.front().colour.z);
    }

    if (!settings.manualSmoothing) {
        const auto presentationSettings = buildPresentationSettings(settings);
        std::vector<float> previousMagnitudes;
        std::array<float, 12> fluxHistory{};
        size_t fluxHistoryIndex = 0;

        if (!sampledIndices.empty() && sampledIndices.front() < sourceSamples.size()) {
            const auto& initialSample = sourceSamples[sampledIndices.front()];
            const auto initialFrame = SpectralPresentation::mixChannels(
                initialSample.magnitudes,
                initialSample.phases,
                initialSample.frequencies,
                initialSample.channels,
                initialSample.sampleRate);
            const float initialLoudness = std::isfinite(initialSample.loudnessLUFS)
                ? initialSample.loudnessLUFS
                : ColourMapper::LOUDNESS_DB_UNSPECIFIED;
            previousMagnitudes = SpectralPresentation::prepareFrame(
                initialFrame,
                presentationSettings,
                initialLoudness).visualiserMagnitudes;
        }

        for (size_t index = 1; index < previewData.size(); ++index) {
            if (index >= sampledIndices.size() || sampledIndices[index] >= sourceSamples.size()) {
                continue;
            }

            const auto& currentSample = sourceSamples[sampledIndices[index]];
            const auto currentFrame = SpectralPresentation::mixChannels(
                currentSample.magnitudes,
                currentSample.phases,
                currentSample.frequencies,
                currentSample.channels,
                currentSample.sampleRate);
            const float currentLoudness = std::isfinite(currentSample.loudnessLUFS)
                ? currentSample.loudnessLUFS
                : ColourMapper::LOUDNESS_DB_UNSPECIFIED;
            SpectralPresentation::Frame previousFrame{};
            const SpectralPresentation::Frame* previousFramePtr = nullptr;
            const double deltaSeconds = previewData[index].timestamp - previewData[index - 1].timestamp;
            if (index > 0 && sampledIndices[index - 1] < sourceSamples.size()) {
                const auto& previousSample = sourceSamples[sampledIndices[index - 1]];
                previousFrame = SpectralPresentation::mixChannels(
                    previousSample.magnitudes,
                    previousSample.phases,
                    previousSample.frequencies,
                    previousSample.channels,
                    previousSample.sampleRate);
                previousFramePtr = &previousFrame;
            }
            const auto preparedFrame = SpectralPresentation::prepareFrame(
                currentFrame,
                presentationSettings,
                currentLoudness,
                previousFramePtr,
                std::isfinite(deltaSeconds) && deltaSeconds > 0.0 ? static_cast<float>(deltaSeconds) : (1.0f / 60.0f));

            SmoothingSignalFeatures features{};
            const auto& modulatedColour = preparedFrame.colourResult;
            features.spectralFlatness = modulatedColour.spectralFlatness;
            features.loudnessNormalised = std::clamp(modulatedColour.loudnessNormalised, 0.0f, 1.0f);

            features.brightnessNormalised = std::clamp(modulatedColour.brightnessNormalised, 0.0f, 1.0f);
            features.phaseInstabilityNorm = std::clamp(modulatedColour.phaseInstabilityNorm, 0.0f, 1.0f);
            features.phaseCoherenceNorm = std::clamp(modulatedColour.phaseCoherenceNorm, 0.0f, 1.0f);
            features.phaseTransientNorm = std::clamp(modulatedColour.phaseTransientNorm, 0.0f, 1.0f);
            populateSpectralNorms(modulatedColour, features);

            float spectralFlux = 0.0f;
            bool fluxComputed = false;
            if (previousMagnitudes.size() == preparedFrame.visualiserMagnitudes.size()) {
                for (size_t magnitudeIndex = 0; magnitudeIndex < preparedFrame.visualiserMagnitudes.size(); ++magnitudeIndex) {
                    const float diff = preparedFrame.visualiserMagnitudes[magnitudeIndex] - previousMagnitudes[magnitudeIndex];
                    spectralFlux += std::max(diff, 0.0f);
                }
                spectralFlux /= static_cast<float>(preparedFrame.visualiserMagnitudes.size());
                fluxComputed = true;
            }
            previousMagnitudes = preparedFrame.visualiserMagnitudes;

            if (fluxComputed) {
                fluxHistory[fluxHistoryIndex] = spectralFlux;
                fluxHistoryIndex = (fluxHistoryIndex + 1) % fluxHistory.size();
            }

            float maxFlux = 0.0f;
            for (const float flux : fluxHistory) {
                maxFlux = std::max(maxFlux, flux);
            }

            features.spectralFlux = fluxComputed ? spectralFlux : 0.0f;
            features.onsetDetected = fluxComputed &&
                maxFlux > 0.0f &&
                spectralFlux > maxFlux * 1.3f &&
                spectralFlux > 0.001f;

            const float deltaTime = std::isfinite(deltaSeconds) && deltaSeconds > 0.0
                ? static_cast<float>(deltaSeconds)
                : (1.0f / 60.0f);

            float targetL = 0.0f;
            float targetA = 0.0f;
            float targetB = 0.0f;
            ColourMapper::XYZtoOklab(
                modulatedColour.X,
                modulatedColour.Y,
                modulatedColour.Z,
                targetL,
                targetA,
                targetB);
            smoother.setTargetOklab(targetL, targetA, targetB);
            smoother.update(deltaTime * 1.2f, features);

            float smoothedL = 0.0f;
            float smoothedA = 0.0f;
            float smoothedB = 0.0f;
            smoother.getCurrentOklab(smoothedL, smoothedA, smoothedB);
            float smoothedX = 0.0f;
            float smoothedY = 0.0f;
            float smoothedZ = 0.0f;
            ColourMapper::OklabtoXYZ(smoothedL, smoothedA, smoothedB, smoothedX, smoothedY, smoothedZ);
            previewData[index] = buildTimelineSampleFromXYZ(
                previewData[index].timestamp,
                smoothedX,
                smoothedY,
                smoothedZ,
                settings);
        }

        return;
    }

    for (size_t index = 1; index < previewData.size(); ++index) {
        const double deltaSeconds = previewData[index].timestamp - previewData[index - 1].timestamp;
        const float deltaTime = std::isfinite(deltaSeconds) && deltaSeconds > 0.0
            ? static_cast<float>(deltaSeconds)
            : (1.0f / 60.0f);

        const auto& currentSample = sourceSamples[sampledIndices[index]];
        const auto currentFrame = SpectralPresentation::mixChannels(
            currentSample.magnitudes,
            currentSample.phases,
            currentSample.frequencies,
            currentSample.channels,
            currentSample.sampleRate);
        const float currentLoudness = std::isfinite(currentSample.loudnessLUFS)
            ? currentSample.loudnessLUFS
            : ColourMapper::LOUDNESS_DB_UNSPECIFIED;
        SpectralPresentation::Frame previousFrame{};
        const SpectralPresentation::Frame* previousFramePtr = nullptr;
        if (index > 0 && sampledIndices[index - 1] < sourceSamples.size()) {
            const auto& previousSample = sourceSamples[sampledIndices[index - 1]];
            previousFrame = SpectralPresentation::mixChannels(
                previousSample.magnitudes,
                previousSample.phases,
                previousSample.frequencies,
                previousSample.channels,
                previousSample.sampleRate);
            previousFramePtr = &previousFrame;
        }
        const auto preparedFrame = SpectralPresentation::prepareFrame(
            currentFrame,
            buildPresentationSettings(settings),
            currentLoudness,
            previousFramePtr,
            std::isfinite(deltaSeconds) && deltaSeconds > 0.0 ? static_cast<float>(deltaSeconds) : (1.0f / 60.0f));
        const auto& modulatedColour = preparedFrame.colourResult;
        float targetL = 0.0f;
        float targetA = 0.0f;
        float targetB = 0.0f;
        ColourMapper::XYZtoOklab(
            modulatedColour.X,
            modulatedColour.Y,
            modulatedColour.Z,
            targetL,
            targetA,
            targetB);
        smoother.setTargetOklab(targetL, targetA, targetB);
        smoother.update(deltaTime * 1.2f);

        float smoothedL = 0.0f;
        float smoothedA = 0.0f;
        float smoothedB = 0.0f;
        smoother.getCurrentOklab(smoothedL, smoothedA, smoothedB);
        float smoothedX = 0.0f;
        float smoothedY = 0.0f;
        float smoothedZ = 0.0f;
        ColourMapper::OklabtoXYZ(smoothedL, smoothedA, smoothedB, smoothedX, smoothedY, smoothedZ);
        previewData[index] = buildTimelineSampleFromXYZ(
            previewData[index].timestamp,
            smoothedX,
            smoothedY,
            smoothedZ,
            settings);
    }
}

}

std::vector<Timeline::TimelineSample> samplePreviewData(
    RecorderState& state,
    size_t maxSamples,
    std::lock_guard<std::mutex>& lock
) {
    (void)lock;
    std::vector<Timeline::TimelineSample> previewData;

    const bool usePreview = state.importPhase == 3 && state.previewReady.load(std::memory_order_acquire);
    const auto& sourceSamples = usePreview ? state.previewSamples : state.samples;

    const size_t samplesSize = sourceSamples.size();
    if (samplesSize == 0) {
        return previewData;
    }

    const auto settings = RecorderColourCache::currentSettings(state);
    if (previewSettingsMatch(state, settings, maxSamples, samplesSize, usePreview)) {
        return state.timelinePreviewCache;
    }
    if (shouldDeferInteractivePreviewRebuild(state)) {
        return state.timelinePreviewCache;
    }
    if (shouldThrottlePreviewRebuild(state)) {
        return state.timelinePreviewCache;
    }

    auto unsmoothedSettings = settings;
    unsmoothedSettings.smoothingEnabled = false;
    unsmoothedSettings.smoothingAmount = 0.0f;
    std::vector<size_t> sampledIndices;

    const auto convertSample = [&](size_t index) {
        const AudioColourSample* previousSample = index > 0 ? &sourceSamples[index - 1] : nullptr;
        return buildTimelineSample(sourceSamples[index], previousSample, unsmoothedSettings);
    };

    if (samplesSize <= maxSamples) {
        previewData.reserve(samplesSize);
        sampledIndices.reserve(samplesSize);
        for (size_t i = 0; i < samplesSize; ++i) {
            sampledIndices.push_back(i);
            previewData.push_back(convertSample(i));
        }
    } else {
        previewData.reserve(maxSamples);
        sampledIndices.reserve(maxSamples);
        const double step = static_cast<double>(samplesSize) / static_cast<double>(maxSamples);
        for (size_t i = 0; i < maxSamples; ++i) {
            const size_t index = static_cast<size_t>(i * step);
            sampledIndices.push_back(index);
            previewData.push_back(convertSample(index));
        }
    }

    applyPreviewSmoothing(previewData, sourceSamples, sampledIndices, settings);
    state.timelinePreviewCache = previewData;
    storePreviewSettings(state, settings, maxSamples, samplesSize, usePreview);
    state.presentationSettingsSettling = false;
    return state.timelinePreviewCache;
}

void renderStatusMessage(RecorderState& state) {
    if (state.statusMessageTimer > 0.0f && !state.statusMessage.empty()) {
        ImGui::Spacing();

        constexpr float FADE_DURATION = 1.0f;
        float alpha = 1.0f;
        if (state.statusMessageTimer < FADE_DURATION) {
            alpha = state.statusMessageTimer / FADE_DURATION;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, alpha * 0.9f));
        ImGui::TextWrapped("%s", state.statusMessage.c_str());
        ImGui::PopStyleColor();
    }
}

}

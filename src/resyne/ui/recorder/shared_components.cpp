#include "resyne/ui/recorder/shared_components.h"

#include <array>
#include <algorithm>
#include <cmath>

#include "imgui.h"

#include "audio/analysis/presentation/sample_sequence.h"
#include "audio/analysis/presentation/spectral_presentation.h"
#include "colour/colour_core.h"
#include "resyne/recorder/colour_cache_utils.h"
#include "ui/smoothing/smoothing.h"
#include "ui/smoothing/smoothing_features.h"

namespace ReSyne::UI {

namespace {

bool previewSettingsMatch(const RecorderState& state,
                          const RecorderColourCache::CacheSettings& settings,
                          const size_t maxSamples,
                          const size_t sourceCount,
                          const bool usePreview) {
    return !state.timelinePreviewCacheDirty &&
        state.timelinePreviewCacheMaxSamples == maxSamples &&
        state.timelinePreviewCacheSourceCount == sourceCount &&
        state.timelinePreviewCacheUsesPreviewSamples == usePreview &&
        state.timelinePreviewCacheColourSpace == settings.colourSpace &&
        state.timelinePreviewCacheGamutMapping == settings.gamutMapping &&
        state.timelinePreviewCacheLowGain == settings.lowGain &&
        state.timelinePreviewCacheMidGain == settings.midGain &&
        state.timelinePreviewCacheHighGain == settings.highGain &&
        state.timelinePreviewCacheSmoothingEnabled == settings.smoothingEnabled &&
        state.timelinePreviewCacheManualSmoothing == settings.manualSmoothing &&
        state.timelinePreviewCacheSmoothingAmount == settings.smoothingAmount;
}

bool nearlyEqual(const float a, const float b) {
    return std::abs(a - b) <= 1e-6f;
}

void storePreviewSettings(RecorderState& state,
                          const RecorderColourCache::CacheSettings& settings,
                          const size_t maxSamples,
                          const size_t sourceCount,
                          const bool usePreview) {
    state.timelinePreviewCacheMaxSamples = maxSamples;
    state.timelinePreviewCacheSourceCount = sourceCount;
    state.timelinePreviewCacheUsesPreviewSamples = usePreview;
    state.timelinePreviewCacheColourSpace = settings.colourSpace;
    state.timelinePreviewCacheGamutMapping = settings.gamutMapping;
    state.timelinePreviewCacheLowGain = settings.lowGain;
    state.timelinePreviewCacheMidGain = settings.midGain;
    state.timelinePreviewCacheHighGain = settings.highGain;
    state.timelinePreviewCacheSmoothingEnabled = settings.smoothingEnabled;
    state.timelinePreviewCacheManualSmoothing = settings.manualSmoothing;
    state.timelinePreviewCacheSmoothingAmount = settings.smoothingAmount;
    state.timelinePreviewCacheDirty = false;
    ++state.timelinePreviewCacheRevision;
}

SpectralPresentation::Settings buildPresentationSettings(const RecorderColourCache::CacheSettings& settings) {
    SpectralPresentation::Settings presentation{};
    presentation.lowGain = settings.lowGain;
    presentation.midGain = settings.midGain;
    presentation.highGain = settings.highGain;
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

Timeline::TimelineSample buildTimelineSampleFromStoredFrame(
    const RSYNPresentationFrame& frame,
    const bool useSmoothedTrack) {
    Timeline::TimelineSample output{};
    output.timestamp = frame.timestamp;
    if (useSmoothedTrack) {
        output.colour = ImVec4(
            std::clamp(frame.smoothedDisplayRgb[0], 0.0f, 1.0f),
            std::clamp(frame.smoothedDisplayRgb[1], 0.0f, 1.0f),
            std::clamp(frame.smoothedDisplayRgb[2], 0.0f, 1.0f),
            1.0f);
        output.labL = frame.smoothedLab[0];
        output.labA = frame.smoothedLab[1];
        output.labB = frame.smoothedLab[2];
    } else {
        output.colour = ImVec4(
            std::clamp(frame.analysis.r, 0.0f, 1.0f),
            std::clamp(frame.analysis.g, 0.0f, 1.0f),
            std::clamp(frame.analysis.b, 0.0f, 1.0f),
            1.0f);
        output.labL = frame.analysis.L;
        output.labA = frame.analysis.a;
        output.labB = frame.analysis.b_comp;
    }
    return output;
}

bool canUseStoredPresentation(const RecorderState& state,
                              const RecorderColourCache::CacheSettings& settings,
                              const bool usePreview,
                              bool& useSmoothedTrack) {
    useSmoothedTrack = false;

    if (usePreview || state.metadata.presentationData == nullptr) {
        return false;
    }

    const auto& presentation = *state.metadata.presentationData;
    if (presentation.frames.empty()) {
        return false;
    }

    if (!state.samples.empty() && presentation.frames.size() != state.samples.size()) {
        return false;
    }

    const auto& storedSettings = presentation.settings;
    const bool baseSettingsMatch =
        storedSettings.colourSpace == settings.colourSpace &&
        storedSettings.applyGamutMapping == settings.gamutMapping &&
        nearlyEqual(storedSettings.lowGain, settings.lowGain) &&
        nearlyEqual(storedSettings.midGain, settings.midGain) &&
        nearlyEqual(storedSettings.highGain, settings.highGain);
    if (!baseSettingsMatch) {
        return false;
    }

    if (!settings.smoothingEnabled) {
        useSmoothedTrack = false;
        return true;
    }

    const bool smoothingMatches =
        storedSettings.smoothingEnabled &&
        storedSettings.manualSmoothing == settings.manualSmoothing &&
        nearlyEqual(storedSettings.smoothingAmount, settings.smoothingAmount);
    if (!smoothingMatches) {
        return false;
    }

    useSmoothedTrack = true;
    return true;
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
    ColourCore::XYZtoLab(X, Y, Z, output.labL, output.labA, output.labB);
    return output;
}

Timeline::TimelineSample buildTimelineSampleFromRGB(const double timestamp,
                                                    const float r,
                                                    const float g,
                                                    const float b,
                                                    const RecorderColourCache::CacheSettings& settings) {
    Timeline::TimelineSample output{};
    output.timestamp = timestamp;
    output.colour = ImVec4(
        std::clamp(r, 0.0f, 1.0f),
        std::clamp(g, 0.0f, 1.0f),
        std::clamp(b, 0.0f, 1.0f),
        1.0f);
    ColourCore::RGBtoLab(
        output.colour.x,
        output.colour.y,
        output.colour.z,
        output.labL,
        output.labA,
        output.labB,
        settings.colourSpace);
    return output;
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
    if (settings.manualSmoothing) {
        smoother.reset(previewData.front().colour.x, previewData.front().colour.y, previewData.front().colour.z);

        for (size_t index = 1; index < previewData.size(); ++index) {
            const double deltaSeconds = previewData[index].timestamp - previewData[index - 1].timestamp;
            const float deltaTime = std::isfinite(deltaSeconds) && deltaSeconds > 0.0
                ? static_cast<float>(deltaSeconds)
                : (1.0f / 60.0f);

            smoother.setTargetColour(
                previewData[index].colour.x,
                previewData[index].colour.y,
                previewData[index].colour.z);
            smoother.update(deltaTime * 1.2f);

            float smoothedR = 0.0f;
            float smoothedG = 0.0f;
            float smoothedB = 0.0f;
            smoother.getCurrentColour(smoothedR, smoothedG, smoothedB);
            previewData[index] = buildTimelineSampleFromRGB(
                previewData[index].timestamp,
                smoothedR,
                smoothedG,
                smoothedB,
                settings);
        }

        return;
    }

    if (!sampledIndices.empty() && sampledIndices.front() < sourceSamples.size()) {
        const size_t initialIndex = sampledIndices.front();
        const auto& initialSample = sourceSamples[initialIndex];
        const AudioColourSample* initialPreviousSample =
            initialIndex > 0 ? &sourceSamples[initialIndex - 1] : nullptr;
        const auto initialPreparedFrame = SpectralPresentation::SampleSequence::prepareSampleFrame(
            initialSample,
            buildPresentationSettings(settings),
            initialPreviousSample);
        float initialL = 0.0f;
        float initialA = 0.0f;
        float initialB = 0.0f;
        ColourCore::XYZtoOklab(
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

    const auto presentationSettings = buildPresentationSettings(settings);
    ::UI::Smoothing::MagnitudeHistory fluxHistory;
    if (!sampledIndices.empty() && sampledIndices.front() < sourceSamples.size()) {
        const size_t initialIndex = sampledIndices.front();
        const auto& initialSample = sourceSamples[initialIndex];
        const AudioColourSample* initialPreviousSample =
            initialIndex > 0 ? &sourceSamples[initialIndex - 1] : nullptr;
        fluxHistory.previousMagnitudes =
            SpectralPresentation::SampleSequence::prepareSampleFrame(
                initialSample,
                presentationSettings,
                initialPreviousSample).visualiserMagnitudes;
    }

    for (size_t index = 1; index < previewData.size(); ++index) {
        if (index >= sampledIndices.size() || sampledIndices[index] >= sourceSamples.size()) {
            continue;
        }

        const size_t sampleIndex = sampledIndices[index];
        const auto& currentSample = sourceSamples[sampleIndex];
        const AudioColourSample* previousSample =
            sampleIndex > 0 ? &sourceSamples[sampleIndex - 1] : nullptr;
        const auto preparedFrame = SpectralPresentation::SampleSequence::prepareSampleFrame(
            currentSample,
            presentationSettings,
            previousSample);

        auto features = ::UI::Smoothing::buildSignalFeatures(preparedFrame.colourResult);
        ::UI::Smoothing::updateFluxHistory(
            preparedFrame.visualiserMagnitudes,
            fluxHistory,
            features);

        const double deltaSeconds = previewData[index].timestamp - previewData[index - 1].timestamp;
        const float deltaTime = std::isfinite(deltaSeconds) && deltaSeconds > 0.0
            ? static_cast<float>(deltaSeconds)
            : (1.0f / 60.0f);

        float targetL = 0.0f;
        float targetA = 0.0f;
        float targetB = 0.0f;
        ColourCore::XYZtoOklab(
            preparedFrame.colourResult.X,
            preparedFrame.colourResult.Y,
            preparedFrame.colourResult.Z,
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
        ColourCore::OklabtoXYZ(smoothedL, smoothedA, smoothedB, smoothedX, smoothedY, smoothedZ);
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

    const auto settings = RecorderColourCache::currentSettings(state);
    const size_t sourceCount = samplesSize > 0
        ? samplesSize
        : (usePreview || state.metadata.presentationData == nullptr ? 0 : state.metadata.presentationData->frames.size());
    if (sourceCount == 0) {
        return previewData;
    }

    if (previewSettingsMatch(state, settings, maxSamples, sourceCount, usePreview)) {
        return state.timelinePreviewCache;
    }

    bool useSmoothedStoredTrack = false;
    if (canUseStoredPresentation(state, settings, usePreview, useSmoothedStoredTrack)) {
        const auto& storedFrames = state.metadata.presentationData->frames;
        if (storedFrames.size() <= maxSamples) {
            previewData.reserve(storedFrames.size());
            for (const auto& frame : storedFrames) {
                previewData.push_back(buildTimelineSampleFromStoredFrame(frame, useSmoothedStoredTrack));
            }
        } else {
            previewData.reserve(maxSamples);
            const double step = static_cast<double>(storedFrames.size()) / static_cast<double>(maxSamples);
            for (size_t i = 0; i < maxSamples; ++i) {
                const size_t frameIndex = static_cast<size_t>(static_cast<double>(i) * step);
                previewData.push_back(buildTimelineSampleFromStoredFrame(
                    storedFrames[std::min(frameIndex, storedFrames.size() - 1)],
                    useSmoothedStoredTrack));
            }
        }

        state.timelinePreviewCache = previewData;
        storePreviewSettings(state, settings, maxSamples, sourceCount, usePreview);
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
            const size_t index = static_cast<size_t>(static_cast<double>(i) * step);
            sampledIndices.push_back(index);
            previewData.push_back(convertSample(index));
        }
    }

    applyPreviewSmoothing(previewData, sourceSamples, sampledIndices, settings);
    state.timelinePreviewCache = previewData;
    storePreviewSettings(state, settings, maxSamples, sourceCount, usePreview);
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

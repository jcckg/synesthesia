#include "resyne/recorder/colour_cache_utils.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "audio/analysis/presentation/sample_sequence.h"
#include "audio/analysis/presentation/spectral_presentation.h"
#include "colour/colour_core.h"
#include "ui/smoothing/smoothing.h"
#include "ui/smoothing/smoothing_features.h"

namespace ReSyne::RecorderColourCache {

namespace {

constexpr float kSmoothingUpdateFactor = 1.2f;
constexpr float kFallbackDeltaTime = 1.0f / 60.0f;

SpectralPresentation::Settings buildPresentationSettings(const CacheSettings& settings) {
    SpectralPresentation::Settings presentation{};
    presentation.lowGain = settings.lowGain;
    presentation.midGain = settings.midGain;
    presentation.highGain = settings.highGain;
    presentation.colourSpace = settings.colourSpace;
    presentation.applyGamutMapping = settings.gamutMapping;
    return presentation;
}

float resolveDeltaTime(const AudioColourSample* previousSample,
                       const AudioColourSample& currentSample) {
    if (previousSample != nullptr) {
        const double delta = currentSample.timestamp - previousSample->timestamp;
        if (std::isfinite(delta) && delta > 0.0) {
            return static_cast<float>(delta);
        }
    }

    return kFallbackDeltaTime;
}

SampleColourEntry entryFromRGB(const float r,
                               const float g,
                               const float b,
                               const CacheSettings& settings) {
    SampleColourEntry entry{};
    entry.rgb.x = std::clamp(r, 0.0f, 1.0f);
    entry.rgb.y = std::clamp(g, 0.0f, 1.0f);
    entry.rgb.z = std::clamp(b, 0.0f, 1.0f);
    entry.rgb.w = 1.0f;

    ColourCore::RGBtoLab(
        entry.rgb.x,
        entry.rgb.y,
        entry.rgb.z,
        entry.labL,
        entry.labA,
        entry.labB,
        settings.colourSpace);

    if (!std::isfinite(entry.labL) || !std::isfinite(entry.labA) || !std::isfinite(entry.labB)) {
        entry.labL = 0.0f;
        entry.labA = 0.0f;
        entry.labB = 0.0f;
    }

    return entry;
}

struct AnalysedEntry {
    SampleColourEntry entry;
    std::vector<float> visualiserMagnitudes;
    ColourCore::FrameResult colourResult;
};

AnalysedEntry computeEntryInternal(const AudioColourSample& sample,
                                  const AudioColourSample* previousSample,
                                  const CacheSettings& settings) {
    const auto preparedFrame = SpectralPresentation::SampleSequence::prepareSampleFrame(
        sample,
        buildPresentationSettings(settings),
        previousSample);
    const auto& colour = preparedFrame.colourResult;

    SampleColourEntry entry{};
    entry.rgb.x = std::clamp(colour.r, 0.0f, 1.0f);
    entry.rgb.y = std::clamp(colour.g, 0.0f, 1.0f);
    entry.rgb.z = std::clamp(colour.b, 0.0f, 1.0f);
    entry.rgb.w = 1.0f;
    entry.labL = colour.L;
    entry.labA = colour.a;
    entry.labB = colour.b_comp;
    return {
        .entry = entry,
        .visualiserMagnitudes = preparedFrame.visualiserMagnitudes,
        .colourResult = colour
    };
}

void smoothEntriesInPlace(std::vector<AnalysedEntry>& entries,
                          const std::vector<AudioColourSample>& samples,
                          const CacheSettings& settings) {
    if (!settings.smoothingEnabled || entries.size() < 2) {
        return;
    }

    SpringSmoother smoother(8.0f, 1.0f, 0.3f);
    smoother.setSmoothingAmount(settings.smoothingAmount);
    smoother.reset(entries.front().entry.rgb.x, entries.front().entry.rgb.y, entries.front().entry.rgb.z);
    entries.front().entry = entryFromRGB(
        entries.front().entry.rgb.x,
        entries.front().entry.rgb.y,
        entries.front().entry.rgb.z,
        settings);
    ::UI::Smoothing::MagnitudeHistory fluxHistory;
    fluxHistory.previousMagnitudes = entries.front().visualiserMagnitudes;

    for (size_t index = 1; index < entries.size(); ++index) {
        const AudioColourSample* previousSample = index > 0 && index - 1 < samples.size()
            ? &samples[index - 1]
            : nullptr;
        const AudioColourSample* currentSample = index < samples.size()
            ? &samples[index]
            : nullptr;

        const float deltaTime = currentSample != nullptr
            ? resolveDeltaTime(previousSample, *currentSample)
            : kFallbackDeltaTime;

        smoother.setTargetColour(
            entries[index].entry.rgb.x,
            entries[index].entry.rgb.y,
            entries[index].entry.rgb.z);

        if (!settings.manualSmoothing) {
            auto features = ::UI::Smoothing::buildSignalFeatures(entries[index].colourResult);
            ::UI::Smoothing::updateFluxHistory(
                entries[index].visualiserMagnitudes,
                fluxHistory,
                features);
            smoother.update(deltaTime * kSmoothingUpdateFactor, features);
        } else {
            smoother.update(deltaTime * kSmoothingUpdateFactor);
        }

        float smoothedR = 0.0f;
        float smoothedG = 0.0f;
        float smoothedB = 0.0f;
        smoother.getCurrentColour(smoothedR, smoothedG, smoothedB);
        entries[index].entry = entryFromRGB(smoothedR, smoothedG, smoothedB, settings);
    }
}

bool settingsMatch(const RecorderState& state,
                   const CacheSettings& settings) {
    return state.colourCacheColourSpace == settings.colourSpace &&
        state.colourCacheGamutMapping == settings.gamutMapping &&
        state.colourCacheLowGain == settings.lowGain &&
        state.colourCacheMidGain == settings.midGain &&
        state.colourCacheHighGain == settings.highGain &&
        state.colourCacheSmoothingEnabled == settings.smoothingEnabled &&
        state.colourCacheManualSmoothing == settings.manualSmoothing &&
        state.colourCacheSmoothingAmount == settings.smoothingAmount &&
        !state.colourCacheDirty;
}

void assignEntry(RecorderState& state,
                 const SampleColourEntry& entry) {
    state.sampleColourCache.push_back(entry);
}

SampleColourEntry smoothEntryAgainstPrevious(const SampleColourEntry& previousEntry,
                                             const AudioColourSample* previousSample,
                                             const AudioColourSample& currentSample,
                                             const AnalysedEntry& currentEntry,
                                             const CacheSettings& settings) {
    if (!settings.smoothingEnabled) {
        return currentEntry.entry;
    }

    SpringSmoother smoother(8.0f, 1.0f, 0.3f);
    smoother.setSmoothingAmount(settings.smoothingAmount);
    smoother.reset(previousEntry.rgb.x, previousEntry.rgb.y, previousEntry.rgb.z);
    smoother.setTargetColour(currentEntry.entry.rgb.x, currentEntry.entry.rgb.y, currentEntry.entry.rgb.z);

    if (!settings.manualSmoothing) {
        auto features = ::UI::Smoothing::buildSignalFeatures(currentEntry.colourResult);
        ::UI::Smoothing::MagnitudeHistory fluxHistory;
        if (previousSample != nullptr) {
            fluxHistory.previousMagnitudes =
                computeEntryInternal(*previousSample, nullptr, settings).visualiserMagnitudes;
        }
        ::UI::Smoothing::updateFluxHistory(
            currentEntry.visualiserMagnitudes,
            fluxHistory,
            features);
        smoother.update(resolveDeltaTime(previousSample, currentSample) * kSmoothingUpdateFactor, features);
    } else {
        smoother.update(resolveDeltaTime(previousSample, currentSample) * kSmoothingUpdateFactor);
    }

    float smoothedR = 0.0f;
    float smoothedG = 0.0f;
    float smoothedB = 0.0f;
    smoother.getCurrentColour(smoothedR, smoothedG, smoothedB);
    return entryFromRGB(smoothedR, smoothedG, smoothedB, settings);
}

}

CacheSettings currentSettings(const RecorderState& state) {
    CacheSettings settings{};
    settings.colourSpace = state.importColourSpace;
    settings.gamutMapping = state.importGamutMapping;
    settings.lowGain = state.importLowGain;
    settings.midGain = state.importMidGain;
    settings.highGain = state.importHighGain;
    settings.smoothingEnabled = state.presentationSmoothingEnabled;
    settings.manualSmoothing = state.presentationManualSmoothing;
    settings.smoothingAmount = state.presentationSmoothingAmount;
    return settings;
}

SampleColourEntry computeSampleColour(const AudioColourSample& sample,
                                      const CacheSettings& settings,
                                      const AudioColourSample* previousSample) {
    return computeEntryInternal(sample, previousSample, settings).entry;
}

void markSettingsIfChanged(RecorderState& state,
                           const CacheSettings& settings) {
    if (state.colourCacheColourSpace != settings.colourSpace ||
        state.colourCacheGamutMapping != settings.gamutMapping ||
        state.colourCacheLowGain != settings.lowGain ||
        state.colourCacheMidGain != settings.midGain ||
        state.colourCacheHighGain != settings.highGain ||
        state.colourCacheSmoothingEnabled != settings.smoothingEnabled ||
        state.colourCacheManualSmoothing != settings.manualSmoothing ||
        state.colourCacheSmoothingAmount != settings.smoothingAmount) {
        state.colourCacheDirty = true;
        state.colourCacheColourSpace = settings.colourSpace;
        state.colourCacheGamutMapping = settings.gamutMapping;
        state.colourCacheLowGain = settings.lowGain;
        state.colourCacheMidGain = settings.midGain;
        state.colourCacheHighGain = settings.highGain;
        state.colourCacheSmoothingEnabled = settings.smoothingEnabled;
        state.colourCacheManualSmoothing = settings.manualSmoothing;
        state.colourCacheSmoothingAmount = settings.smoothingAmount;
    }
}

void ensureCacheLocked(RecorderState& state) {
    const CacheSettings settings = currentSettings(state);
    markSettingsIfChanged(state, settings);
    if (!state.colourCacheDirty && state.sampleColourCache.size() == state.samples.size()) {
        return;
    }

    std::vector<AnalysedEntry> entries;
    entries.reserve(state.samples.size());
    for (size_t index = 0; index < state.samples.size(); ++index) {
        const AudioColourSample* previousSample = index > 0 ? &state.samples[index - 1] : nullptr;
        entries.push_back(computeEntryInternal(state.samples[index], previousSample, settings));
    }

    smoothEntriesInPlace(entries, state.samples, settings);
    state.sampleColourCache.clear();
    state.sampleColourCache.reserve(entries.size());
    for (auto& analysedEntry : entries) {
        state.sampleColourCache.push_back(std::move(analysedEntry.entry));
    }
    state.colourCacheDirty = false;
}

void appendSampleLocked(RecorderState& state,
                        const AudioColourSample& sample) {
    const CacheSettings settings = currentSettings(state);
    if (!settingsMatch(state, settings)) {
        state.colourCacheDirty = true;
        return;
    }

    const AudioColourSample* previousSample = state.samples.size() >= 2 ? &state.samples[state.samples.size() - 2] : nullptr;
    AnalysedEntry entryData = computeEntryInternal(sample, previousSample, settings);
    SampleColourEntry entry = entryData.entry;
    if (!state.sampleColourCache.empty() && state.samples.size() >= 2) {
        entry = smoothEntryAgainstPrevious(
            state.sampleColourCache.back(),
            previousSample,
            sample,
            entryData,
            settings);
    }

    assignEntry(state, entry);
}

void rebuildCache(RecorderState& state) {
    std::lock_guard<std::mutex> lock(state.samplesMutex);
    ensureCacheLocked(state);
}

}

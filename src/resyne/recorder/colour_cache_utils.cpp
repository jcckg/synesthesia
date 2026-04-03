#include "resyne/recorder/colour_cache_utils.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "audio/analysis/presentation/spectral_presentation.h"
#include "ui/smoothing/smoothing.h"

namespace ReSyne::RecorderColourCache {

namespace {

constexpr float kSmoothingUpdateFactor = 1.2f;
constexpr float kFallbackDeltaTime = 1.0f / 60.0f;

SpectralPresentation::Settings buildPresentationSettings(const CacheSettings& settings) {
    SpectralPresentation::Settings presentation{};
    presentation.lowGain = settings.lowGain;
    presentation.midGain = settings.midGain;
    presentation.highGain = settings.highGain;
    presentation.gamma = settings.gamma;
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

    ColourMapper::RGBtoLab(
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

SampleColourEntry computeEntryInternal(const AudioColourSample& sample,
                                       const CacheSettings& settings) {
    const float loudnessOverride = std::isfinite(sample.loudnessLUFS)
        ? sample.loudnessLUFS
        : ColourMapper::LOUDNESS_DB_UNSPECIFIED;
    const SpectralPresentation::Frame frame = SpectralPresentation::mixChannels(
        sample.magnitudes,
        sample.phases,
        sample.frequencies,
        sample.channels,
        sample.sampleRate);
    const auto colour = SpectralPresentation::buildColourResult(
        frame,
        buildPresentationSettings(settings),
        loudnessOverride);

    SampleColourEntry entry{};
    entry.rgb.x = std::clamp(colour.r, 0.0f, 1.0f);
    entry.rgb.y = std::clamp(colour.g, 0.0f, 1.0f);
    entry.rgb.z = std::clamp(colour.b, 0.0f, 1.0f);
    entry.rgb.w = 1.0f;
    entry.labL = colour.L;
    entry.labA = colour.a;
    entry.labB = colour.b_comp;
    return entry;
}

void smoothEntriesInPlace(std::vector<SampleColourEntry>& entries,
                          const std::vector<AudioColourSample>& samples,
                          const CacheSettings& settings) {
    if (!settings.smoothingEnabled || !settings.manualSmoothing || entries.size() < 2) {
        return;
    }

    SpringSmoother smoother(8.0f, 1.0f, 0.3f);
    smoother.setSmoothingAmount(settings.smoothingAmount);
    smoother.reset(entries.front().rgb.x, entries.front().rgb.y, entries.front().rgb.z);
    entries.front() = entryFromRGB(entries.front().rgb.x, entries.front().rgb.y, entries.front().rgb.z, settings);

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

        smoother.setTargetColour(entries[index].rgb.x, entries[index].rgb.y, entries[index].rgb.z);
        smoother.update(deltaTime * kSmoothingUpdateFactor);

        float smoothedR = 0.0f;
        float smoothedG = 0.0f;
        float smoothedB = 0.0f;
        smoother.getCurrentColour(smoothedR, smoothedG, smoothedB);
        entries[index] = entryFromRGB(smoothedR, smoothedG, smoothedB, settings);
    }
}

bool settingsMatch(const RecorderState& state,
                   const CacheSettings& settings) {
    return state.colourCacheGamma == settings.gamma &&
        state.colourCacheColourSpace == settings.colourSpace &&
        state.colourCacheGamutMapping == settings.gamutMapping &&
        !state.colourCacheDirty;
}

void assignEntry(RecorderState& state,
                 const SampleColourEntry& entry) {
    state.sampleColourCache.push_back(entry);
}

SampleColourEntry smoothEntryAgainstPrevious(const SampleColourEntry& previousEntry,
                                             const AudioColourSample* previousSample,
                                             const AudioColourSample& currentSample,
                                             const SampleColourEntry& currentEntry,
                                             const CacheSettings& settings) {
    if (!settings.smoothingEnabled || !settings.manualSmoothing) {
        return currentEntry;
    }

    SpringSmoother smoother(8.0f, 1.0f, 0.3f);
    smoother.setSmoothingAmount(settings.smoothingAmount);
    smoother.reset(previousEntry.rgb.x, previousEntry.rgb.y, previousEntry.rgb.z);
    smoother.setTargetColour(currentEntry.rgb.x, currentEntry.rgb.y, currentEntry.rgb.z);
    smoother.update(resolveDeltaTime(previousSample, currentSample) * kSmoothingUpdateFactor);

    float smoothedR = 0.0f;
    float smoothedG = 0.0f;
    float smoothedB = 0.0f;
    smoother.getCurrentColour(smoothedR, smoothedG, smoothedB);
    return entryFromRGB(smoothedR, smoothedG, smoothedB, settings);
}

}

CacheSettings currentSettings(const RecorderState& state) {
    CacheSettings settings{};
    settings.gamma = state.importGamma;
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
                                      const CacheSettings& settings) {
    return computeEntryInternal(sample, settings);
}

void markSettingsIfChanged(RecorderState& state,
                           const CacheSettings& settings) {
    if (state.colourCacheGamma != settings.gamma ||
        state.colourCacheColourSpace != settings.colourSpace ||
        state.colourCacheGamutMapping != settings.gamutMapping) {
        state.colourCacheDirty = true;
        state.colourCacheGamma = settings.gamma;
        state.colourCacheColourSpace = settings.colourSpace;
        state.colourCacheGamutMapping = settings.gamutMapping;
    }
}

void ensureCacheLocked(RecorderState& state) {
    const CacheSettings settings = currentSettings(state);
    markSettingsIfChanged(state, settings);
    if (!state.colourCacheDirty && state.sampleColourCache.size() == state.samples.size()) {
        return;
    }

    std::vector<SampleColourEntry> entries;
    entries.reserve(state.samples.size());
    for (const auto& sample : state.samples) {
        entries.push_back(computeEntryInternal(sample, settings));
    }

    smoothEntriesInPlace(entries, state.samples, settings);
    state.sampleColourCache = std::move(entries);
    state.colourCacheDirty = false;
}

void appendSampleLocked(RecorderState& state,
                        const AudioColourSample& sample) {
    const CacheSettings settings = currentSettings(state);
    if (!settingsMatch(state, settings)) {
        state.colourCacheDirty = true;
        return;
    }

    SampleColourEntry entry = computeEntryInternal(sample, settings);
    if (!state.sampleColourCache.empty() && state.samples.size() >= 2) {
        const auto& previousSample = state.samples[state.samples.size() - 2];
        entry = smoothEntryAgainstPrevious(
            state.sampleColourCache.back(),
            &previousSample,
            sample,
            entry,
            settings);
    }

    assignEntry(state, entry);
}

void rebuildCache(RecorderState& state) {
    std::lock_guard<std::mutex> lock(state.samplesMutex);
    ensureCacheLocked(state);
}

}

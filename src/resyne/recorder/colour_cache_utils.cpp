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
constexpr auto kInteractiveRebuildDebounce = std::chrono::milliseconds(120);

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

struct CacheFrameData {
    SampleColourEntry entry;
    std::vector<float> visualiserMagnitudes;
    ColourMapper::ColourResult colourResult;
};

CacheFrameData computeEntryInternal(const AudioColourSample& sample,
                                    const AudioColourSample* previousSample,
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
    SpectralPresentation::Frame previousFrame{};
    const SpectralPresentation::Frame* previousFramePtr = nullptr;
    if (previousSample != nullptr) {
        previousFrame = SpectralPresentation::mixChannels(
            previousSample->magnitudes,
            previousSample->phases,
            previousSample->frequencies,
            previousSample->channels,
            previousSample->sampleRate);
        previousFramePtr = &previousFrame;
    }
        
    const auto preparedFrame = SpectralPresentation::prepareFrame(
        frame,
        buildPresentationSettings(settings),
        loudnessOverride,
        previousFramePtr,
        resolveDeltaTime(previousSample, sample));

    SampleColourEntry entry{};
    entry.rgb.x = std::clamp(preparedFrame.colourResult.r, 0.0f, 1.0f);
    entry.rgb.y = std::clamp(preparedFrame.colourResult.g, 0.0f, 1.0f);
    entry.rgb.z = std::clamp(preparedFrame.colourResult.b, 0.0f, 1.0f);
    entry.rgb.w = 1.0f;
    entry.labL = preparedFrame.colourResult.L;
    entry.labA = preparedFrame.colourResult.a;
    entry.labB = preparedFrame.colourResult.b_comp;
    
    return {entry, preparedFrame.visualiserMagnitudes, preparedFrame.colourResult};
}

void smoothEntriesInPlace(std::vector<CacheFrameData>& entries,
                          const std::vector<AudioColourSample>& samples,
                          const CacheSettings& settings) {
    if (!settings.smoothingEnabled || entries.size() < 2) {
        return;
    }

    SpringSmoother smoother(8.0f, 1.0f, 0.3f);
    smoother.setSmoothingAmount(settings.smoothingAmount);
    smoother.reset(entries.front().entry.rgb.x, entries.front().entry.rgb.y, entries.front().entry.rgb.z);
    entries.front().entry = entryFromRGB(entries.front().entry.rgb.x, entries.front().entry.rgb.y, entries.front().entry.rgb.z, settings);

    std::vector<float> previousMagnitudes;
    std::array<float, 12> fluxHistory{};
    size_t fluxHistoryIndex = 0;

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

        smoother.setTargetColour(entries[index].entry.rgb.x, entries[index].entry.rgb.y, entries[index].entry.rgb.z);

        if (!settings.manualSmoothing) {
            float playbackFlux = 0.0f;
            bool fluxComputed = false;
            const auto& visualiserMagnitudes = entries[index].visualiserMagnitudes;
            if (previousMagnitudes.size() == visualiserMagnitudes.size()) {
                for (size_t i = 0; i < visualiserMagnitudes.size(); ++i) {
                    const float diff = visualiserMagnitudes[i] - previousMagnitudes[i];
                    playbackFlux += std::max(diff, 0.0f);
                }
                if (!visualiserMagnitudes.empty()) {
                    playbackFlux /= static_cast<float>(visualiserMagnitudes.size());
                }
                fluxComputed = true;
            }
            previousMagnitudes = visualiserMagnitudes;

            if (fluxComputed) {
                fluxHistory[fluxHistoryIndex] = playbackFlux;
                fluxHistoryIndex = (fluxHistoryIndex + 1) % fluxHistory.size();
            }

            float maxFlux = 0.0f;
            for (const float flux : fluxHistory) {
                maxFlux = std::max(maxFlux, flux);
            }

            const bool playbackOnset = fluxComputed &&
                maxFlux > 0.0f &&
                playbackFlux > maxFlux * 1.3f &&
                playbackFlux > 0.001f;

            SmoothingSignalFeatures features{};
            features.onsetDetected = playbackOnset;
            features.spectralFlux = fluxComputed ? playbackFlux : 0.0f;
            features.spectralFlatness = entries[index].colourResult.spectralFlatness;
            features.loudnessNormalised = std::clamp(entries[index].colourResult.loudnessNormalised, 0.0f, 1.0f);
            features.brightnessNormalised = std::clamp(entries[index].colourResult.brightnessNormalised, 0.0f, 1.0f);
            features.phaseInstabilityNorm = std::clamp(entries[index].colourResult.phaseInstabilityNorm, 0.0f, 1.0f);
            features.phaseCoherenceNorm = std::clamp(entries[index].colourResult.phaseCoherenceNorm, 0.0f, 1.0f);
            features.phaseTransientNorm = std::clamp(entries[index].colourResult.phaseTransientNorm, 0.0f, 1.0f);
            populateSpectralNorms(entries[index].colourResult, features);

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
    return state.colourCacheGamma == settings.gamma &&
        state.colourCacheColourSpace == settings.colourSpace &&
        state.colourCacheGamutMapping == settings.gamutMapping &&
        state.colourCacheLowGain == settings.lowGain &&
        state.colourCacheMidGain == settings.midGain &&
        state.colourCacheHighGain == settings.highGain &&
        state.colourCacheSmoothingEnabled == settings.smoothingEnabled &&
        state.colourCacheManualSmoothing == settings.manualSmoothing &&
        state.colourCacheSmoothingAmount == settings.smoothingAmount &&
        !state.colourCacheDirty;
}

bool shouldDeferInteractiveRebuild(const RecorderState& state) {
    if (!state.presentationSettingsSettling) {
        return false;
    }

    using namespace std::chrono;
    const auto elapsed = duration_cast<milliseconds>(
        steady_clock::now() - state.presentationSettingsLastChangedTime);
    return elapsed < kInteractiveRebuildDebounce;
}

void assignEntry(RecorderState& state,
                 const SampleColourEntry& entry) {
    state.sampleColourCache.push_back(entry);
}

SampleColourEntry smoothEntryAgainstPrevious(const SampleColourEntry& previousEntry,
                                             const AudioColourSample* previousSample,
                                             const AudioColourSample& currentSample,
                                             const CacheFrameData& currentEntryData,
                                             const CacheSettings& settings) {
    if (!settings.smoothingEnabled) {
        return currentEntryData.entry;
    }

    SpringSmoother smoother(8.0f, 1.0f, 0.3f);
    smoother.setSmoothingAmount(settings.smoothingAmount);
    smoother.reset(previousEntry.rgb.x, previousEntry.rgb.y, previousEntry.rgb.z);
    smoother.setTargetColour(currentEntryData.entry.rgb.x, currentEntryData.entry.rgb.y, currentEntryData.entry.rgb.z);

    if (!settings.manualSmoothing && previousSample != nullptr) {
        // Compute features for single frame
        CacheFrameData previousData = computeEntryInternal(*previousSample, nullptr, settings);
        
        float playbackFlux = 0.0f;
        bool fluxComputed = false;
        if (previousData.visualiserMagnitudes.size() == currentEntryData.visualiserMagnitudes.size()) {
            for (size_t i = 0; i < currentEntryData.visualiserMagnitudes.size(); ++i) {
                const float diff = currentEntryData.visualiserMagnitudes[i] - previousData.visualiserMagnitudes[i];
                playbackFlux += std::max(diff, 0.0f);
            }
            if (!currentEntryData.visualiserMagnitudes.empty()) {
                playbackFlux /= static_cast<float>(currentEntryData.visualiserMagnitudes.size());
            }
            fluxComputed = true;
        }
        
        const bool playbackOnset = fluxComputed && playbackFlux > 0.001f; // simplistic subset of array flux logic since history isn't maintained
        
        SmoothingSignalFeatures features{};
        features.onsetDetected = playbackOnset;
        features.spectralFlux = fluxComputed ? playbackFlux : 0.0f;
        features.spectralFlatness = currentEntryData.colourResult.spectralFlatness;
        features.loudnessNormalised = std::clamp(currentEntryData.colourResult.loudnessNormalised, 0.0f, 1.0f);
        features.brightnessNormalised = std::clamp(currentEntryData.colourResult.brightnessNormalised, 0.0f, 1.0f);
        features.phaseInstabilityNorm = std::clamp(currentEntryData.colourResult.phaseInstabilityNorm, 0.0f, 1.0f);
        features.phaseCoherenceNorm = std::clamp(currentEntryData.colourResult.phaseCoherenceNorm, 0.0f, 1.0f);
        features.phaseTransientNorm = std::clamp(currentEntryData.colourResult.phaseTransientNorm, 0.0f, 1.0f);
        populateSpectralNorms(currentEntryData.colourResult, features);

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
                                      const CacheSettings& settings,
                                      const AudioColourSample* previousSample) {
    return computeEntryInternal(sample, previousSample, settings).entry;
}

void markSettingsIfChanged(RecorderState& state,
                           const CacheSettings& settings) {
    if (state.colourCacheGamma != settings.gamma ||
        state.colourCacheColourSpace != settings.colourSpace ||
        state.colourCacheGamutMapping != settings.gamutMapping ||
        state.colourCacheLowGain != settings.lowGain ||
        state.colourCacheMidGain != settings.midGain ||
        state.colourCacheHighGain != settings.highGain ||
        state.colourCacheSmoothingEnabled != settings.smoothingEnabled ||
        state.colourCacheManualSmoothing != settings.manualSmoothing ||
        state.colourCacheSmoothingAmount != settings.smoothingAmount) {
        state.colourCacheDirty = true;
        state.colourCacheGamma = settings.gamma;
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
    if (shouldDeferInteractiveRebuild(state)) {
        return;
    }

    std::vector<CacheFrameData> frameDataList;
    frameDataList.reserve(state.samples.size());
    for (size_t index = 0; index < state.samples.size(); ++index) {
        const AudioColourSample* previousSample = index > 0 ? &state.samples[index - 1] : nullptr;
        frameDataList.push_back(computeEntryInternal(state.samples[index], previousSample, settings));
    }

    smoothEntriesInPlace(frameDataList, state.samples, settings);
    
    std::vector<SampleColourEntry> finalEntries;
    finalEntries.reserve(frameDataList.size());
    for (auto& fd : frameDataList) {
        finalEntries.push_back(std::move(fd.entry));
    }

    state.sampleColourCache = std::move(finalEntries);
    state.colourCacheDirty = false;
    state.presentationSettingsSettling = false;
}

void appendSampleLocked(RecorderState& state,
                        const AudioColourSample& sample) {
    const CacheSettings settings = currentSettings(state);
    if (!settingsMatch(state, settings)) {
        state.colourCacheDirty = true;
        return;
    }

    const AudioColourSample* previousSampleForEntry = state.samples.size() >= 2 ? &state.samples[state.samples.size() - 2] : nullptr;
    CacheFrameData entryData = computeEntryInternal(sample, previousSampleForEntry, settings);
    SampleColourEntry entry = entryData.entry;
    if (!state.sampleColourCache.empty() && state.samples.size() >= 2) {
        const auto& previousSample = state.samples[state.samples.size() - 2];
        entry = smoothEntryAgainstPrevious(
            state.sampleColourCache.back(),
            &previousSample,
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

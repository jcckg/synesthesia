#include "resyne/recorder/colour_cache_utils.h"

#include "colour/colour_mapper.h"

#include <cmath>

namespace ReSyne::RecorderColourCache {

namespace {

void assignEntry(RecorderState& state,
				 size_t index,
				 const SampleColourEntry& entry) {
	if (index >= state.sampleColourCache.size()) {
		state.sampleColourCache.resize(index + 1);
	}
	state.sampleColourCache[index] = entry;
}

SampleColourEntry computeEntryInternal(const AudioColourSample& sample,
	float gamma,
	ColourMapper::ColourSpace colourSpace,
	bool gamutMapping) {
	const float loudnessOverride = std::isfinite(sample.loudnessLUFS)
		? sample.loudnessLUFS
		: ColourMapper::LOUDNESS_DB_UNSPECIFIED;

	std::vector<float> averagedMagnitudes;
	std::vector<float> averagedPhases;

	if (!sample.magnitudes.empty() && !sample.magnitudes[0].empty()) {
		const size_t numBins = sample.magnitudes[0].size();
		const uint32_t numChannels = sample.channels;

		averagedMagnitudes.resize(numBins, 0.0f);
		averagedPhases.resize(numBins, 0.0f);

		for (uint32_t ch = 0; ch < numChannels && ch < sample.magnitudes.size(); ++ch) {
			if (sample.magnitudes[ch].size() == numBins) {
				for (size_t bin = 0; bin < numBins; ++bin) {
					const float mag = sample.magnitudes[ch][bin];
					averagedMagnitudes[bin] += mag * mag;
				}
			}
			if (ch < sample.phases.size() && sample.phases[ch].size() == numBins) {
				for (size_t bin = 0; bin < numBins; ++bin) {
					averagedPhases[bin] += sample.phases[ch][bin];
				}
			}
		}

		if (numChannels > 0) {
			const float invChannels = 1.0f / static_cast<float>(numChannels);
			for (size_t bin = 0; bin < numBins; ++bin) {
				averagedMagnitudes[bin] = std::sqrt(averagedMagnitudes[bin] * invChannels);
				averagedPhases[bin] *= invChannels;
			}
		}
	}

	const auto colour = ColourMapper::spectrumToColour(
		averagedMagnitudes,
		averagedPhases,
		sample.sampleRate,
		gamma,
		colourSpace,
		gamutMapping,
		loudnessOverride);

	SampleColourEntry entry{};
	entry.rgb = ImVec4(colour.r, colour.g, colour.b, 1.0f);
	ColourMapper::RGBtoLab(
		entry.rgb.x,
		entry.rgb.y,
		entry.rgb.z,
		entry.labL,
		entry.labA,
		entry.labB,
		colourSpace);
	return entry;
}

bool settingsMatch(const RecorderState& state,
					 float gamma,
					 ColourMapper::ColourSpace colourSpace,
					 bool gamutMapping) {
	return state.colourCacheGamma == gamma &&
		state.colourCacheColourSpace == colourSpace &&
		state.colourCacheGamutMapping == gamutMapping &&
		!state.colourCacheDirty;
}

}

SampleColourEntry computeSampleColour(const AudioColourSample& sample,
	float gamma,
	ColourMapper::ColourSpace colourSpace,
	bool gamutMapping) {
	return computeEntryInternal(sample, gamma, colourSpace, gamutMapping);
}

void markSettingsIfChanged(RecorderState& state,
	float gamma,
	ColourMapper::ColourSpace colourSpace,
	bool gamutMapping) {
	if (state.colourCacheGamma != gamma ||
		state.colourCacheColourSpace != colourSpace ||
		state.colourCacheGamutMapping != gamutMapping) {
		state.colourCacheDirty = true;
		state.colourCacheGamma = gamma;
		state.colourCacheColourSpace = colourSpace;
		state.colourCacheGamutMapping = gamutMapping;
	}
}

void ensureCacheLocked(RecorderState& state) {
	markSettingsIfChanged(state,
		state.importGamma,
		state.importColourSpace,
		state.importGamutMapping);
	if (!state.colourCacheDirty &&
		state.sampleColourCache.size() == state.samples.size()) {
		return;
	}
	state.sampleColourCache.resize(state.samples.size());
	for (size_t i = 0; i < state.samples.size(); ++i) {
		state.sampleColourCache[i] = computeEntryInternal(
			state.samples[i],
			state.importGamma,
			state.importColourSpace,
			state.importGamutMapping);
	}
	state.colourCacheDirty = false;
}

void appendSampleLocked(RecorderState& state,
	const AudioColourSample& sample) {
	if (!settingsMatch(state,
					state.importGamma,
					state.importColourSpace,
					state.importGamutMapping)) {
		state.colourCacheDirty = true;
		return;
	}
	assignEntry(state,
			state.sampleColourCache.size(),
			computeEntryInternal(sample,
				state.importGamma,
				state.importColourSpace,
				state.importGamutMapping));
}

void rebuildCache(RecorderState& state) {
	std::lock_guard<std::mutex> lock(state.samplesMutex);
	markSettingsIfChanged(state,
		state.importGamma,
		state.importColourSpace,
		state.importGamutMapping);
	state.sampleColourCache.resize(state.samples.size());
	for (size_t i = 0; i < state.samples.size(); ++i) {
		state.sampleColourCache[i] = computeEntryInternal(
			state.samples[i],
			state.importGamma,
			state.importColourSpace,
			state.importGamutMapping);
	}
	state.colourCacheDirty = false;
}

}

#include "varispeed.h"

#include <algorithm>
#include <cmath>

namespace Varispeed {

namespace {
constexpr float EPSILON = 1e-6f;
constexpr float MIN_REGION_FRAMES = 4;
constexpr float RATIO_TOLERANCE = 0.05f;
}

std::vector<VarspeedRegion> detectVarispeedRegions(
	const std::vector<std::vector<float>>& allFrequencies,
	float sampleRate,
	size_t fftSize,
	float minShiftRatio
) {
	std::vector<VarspeedRegion> regions;

	if (allFrequencies.empty() || fftSize == 0 || sampleRate <= 0.0f) {
		return regions;
	}

	const size_t numFrames = allFrequencies.size();
	const float freqResolution = sampleRate / static_cast<float>(fftSize);

	std::vector<float> frameRatios(numFrames, 1.0f);

	for (size_t frame = 0; frame < numFrames; ++frame) {
		const auto& freqs = allFrequencies[frame];
		if (freqs.empty()) {
			continue;
		}

		float ratioSum = 0.0f;
		float weightSum = 0.0f;

		for (size_t bin = 1; bin < freqs.size(); ++bin) {
			const float expectedFreq = freqResolution * static_cast<float>(bin);
			const float decodedFreq = freqs[bin];

			if (expectedFreq > EPSILON && decodedFreq > EPSILON) {
				const float ratio = decodedFreq / expectedFreq;
				if (ratio > 0.1f && ratio < 10.0f) {
					ratioSum += ratio;
					weightSum += 1.0f;
				}
			}
		}

		if (weightSum > 0.0f) {
			frameRatios[frame] = ratioSum / weightSum;
		}
	}

	size_t regionStart = 0;
	float currentRatio = frameRatios[0];
	bool inRegion = std::abs(currentRatio - 1.0f) > minShiftRatio;

	for (size_t frame = 1; frame <= numFrames; ++frame) {
		const float ratio = frame < numFrames ? frameRatios[frame] : 1.0f;
		const bool isShifted = std::abs(ratio - 1.0f) > minShiftRatio;
		const bool ratioChanged = std::abs(ratio - currentRatio) > RATIO_TOLERANCE;

		if (inRegion && (!isShifted || ratioChanged || frame == numFrames)) {
			if (frame - regionStart >= MIN_REGION_FRAMES) {
				VarspeedRegion region;
				region.startFrame = regionStart;
				region.endFrame = frame;
				region.pitchRatio = currentRatio;
				regions.push_back(region);
			}
			inRegion = false;
		}

		if (isShifted && !inRegion) {
			regionStart = frame;
			currentRatio = ratio;
			inRegion = true;
		} else if (isShifted && inRegion && !ratioChanged) {
			currentRatio = (currentRatio + ratio) * 0.5f;
		}
	}

	return regions;
}

std::vector<float> resampleAudio(
	const std::vector<float>& input,
	float pitchRatio
) {
	if (input.empty() || pitchRatio <= EPSILON) {
		return input;
	}

	if (std::abs(pitchRatio - 1.0f) < EPSILON) {
		return input;
	}

	const size_t inputSize = input.size();
	const size_t outputSize = static_cast<size_t>(
		std::ceil(static_cast<float>(inputSize) / pitchRatio)
	);

	std::vector<float> output(outputSize);

	for (size_t i = 0; i < outputSize; ++i) {
		const float srcPos = static_cast<float>(i) * pitchRatio;
		const size_t idx0 = static_cast<size_t>(srcPos);
		const float frac = srcPos - static_cast<float>(idx0);

		if (idx0 + 1 < inputSize) {
			output[i] = input[idx0] * (1.0f - frac) + input[idx0 + 1] * frac;
		} else if (idx0 < inputSize) {
			output[i] = input[idx0];
		} else {
			output[i] = 0.0f;
		}
	}

	return output;
}

std::vector<float> applyVarispeedRegions(
	const std::vector<float>& audio,
	const std::vector<VarspeedRegion>& regions,
	int hopSize,
	size_t crossfadeSamples
) {
	if (audio.empty() || regions.empty() || hopSize <= 0) {
		return audio;
	}

	std::vector<std::pair<size_t, size_t>> sampleRegions;
	for (const auto& region : regions) {
		sampleRegions.push_back({
			region.startFrame * static_cast<size_t>(hopSize),
			region.endFrame * static_cast<size_t>(hopSize)
		});
	}

	std::vector<float> result;
	result.reserve(audio.size());

	size_t currentPos = 0;

	for (size_t i = 0; i < regions.size(); ++i) {
		const auto& region = regions[i];
		const size_t startSample = region.startFrame * static_cast<size_t>(hopSize);
		const size_t endSample = std::min(
			region.endFrame * static_cast<size_t>(hopSize),
			audio.size()
		);

		if (startSample > currentPos) {
			result.insert(result.end(),
				audio.begin() + static_cast<ptrdiff_t>(currentPos),
				audio.begin() + static_cast<ptrdiff_t>(startSample));
		}

		if (startSample < endSample && endSample <= audio.size()) {
			std::vector<float> regionAudio(
				audio.begin() + static_cast<ptrdiff_t>(startSample),
				audio.begin() + static_cast<ptrdiff_t>(endSample)
			);

			std::vector<float> resampled = resampleAudio(regionAudio, region.pitchRatio);

			const size_t fadeLen = std::min(crossfadeSamples, resampled.size() / 2);

			if (fadeLen > 0 && !result.empty()) {
				const size_t overlapStart = result.size() > fadeLen ? result.size() - fadeLen : 0;
				for (size_t j = 0; j < fadeLen && j < resampled.size(); ++j) {
					const float t = static_cast<float>(j) / static_cast<float>(fadeLen);
					const float fadeOut = 0.5f * (1.0f + std::cos(t * 3.14159265f));
					const float fadeIn = 1.0f - fadeOut;

					if (overlapStart + j < result.size()) {
						result[overlapStart + j] = result[overlapStart + j] * fadeOut +
							resampled[j] * fadeIn;
					}
				}
				result.insert(result.end(),
					resampled.begin() + static_cast<ptrdiff_t>(fadeLen),
					resampled.end());
			} else {
				result.insert(result.end(), resampled.begin(), resampled.end());
			}
		}

		currentPos = endSample;
	}

	if (currentPos < audio.size()) {
		const size_t fadeLen = std::min(crossfadeSamples, audio.size() - currentPos);

		if (fadeLen > 0 && !result.empty()) {
			const size_t overlapStart = result.size() > fadeLen ? result.size() - fadeLen : 0;
			for (size_t j = 0; j < fadeLen; ++j) {
				const float t = static_cast<float>(j) / static_cast<float>(fadeLen);
				const float fadeOut = 0.5f * (1.0f + std::cos(t * 3.14159265f));
				const float fadeIn = 1.0f - fadeOut;

				if (overlapStart + j < result.size() && currentPos + j < audio.size()) {
					result[overlapStart + j] = result[overlapStart + j] * fadeOut +
						audio[currentPos + j] * fadeIn;
				}
			}
			result.insert(result.end(),
				audio.begin() + static_cast<ptrdiff_t>(currentPos + fadeLen),
				audio.end());
		} else {
			result.insert(result.end(),
				audio.begin() + static_cast<ptrdiff_t>(currentPos),
				audio.end());
		}
	}

	return result;
}

}

#pragma once

#include <cstddef>
#include <vector>

namespace Varispeed {

struct VarspeedRegion {
	size_t startFrame;
	size_t endFrame;
	float pitchRatio;
};

std::vector<VarspeedRegion> detectVarispeedRegions(
	const std::vector<std::vector<float>>& allFrequencies,
	float sampleRate,
	size_t fftSize,
	float minShiftRatio = 0.02f
);

std::vector<float> resampleAudio(
	const std::vector<float>& input,
	float pitchRatio
);

std::vector<float> applyVarispeedRegions(
	const std::vector<float>& audio,
	const std::vector<VarspeedRegion>& regions,
	int hopSize,
	size_t crossfadeSamples = 256
);

}

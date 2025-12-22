#include "boundary_phase_interpolation.h"
#include "phase_wrapping.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace PhaseReconstruction {

namespace {
constexpr float EPSILON = 1e-6f;


void slerpPhaseVectors(float cosOriginal, float sinOriginal,
					   float cosReconstructed, float sinReconstructed,
					   float weight,
					   float& cosResult, float& sinResult) {
	const float lenOrig = std::sqrt(cosOriginal * cosOriginal + sinOriginal * sinOriginal);
	const float lenRecon = std::sqrt(cosReconstructed * cosReconstructed +
									 sinReconstructed * sinReconstructed);

	if (lenOrig < EPSILON || lenRecon < EPSILON) {
		if (lenOrig >= EPSILON) {
			cosResult = cosOriginal / lenOrig;
			sinResult = sinOriginal / lenOrig;
		} else if (lenRecon >= EPSILON) {
			cosResult = cosReconstructed / lenRecon;
			sinResult = sinReconstructed / lenRecon;
		} else {
			cosResult = 1.0f;
			sinResult = 0.0f;
		}
		return;
	}

	const float nCosOrig = cosOriginal / lenOrig;
	const float nSinOrig = sinOriginal / lenOrig;
	const float nCosRecon = cosReconstructed / lenRecon;
	const float nSinRecon = sinReconstructed / lenRecon;

	float dot = nCosOrig * nCosRecon + nSinOrig * nSinRecon;
	dot = std::clamp(dot, -1.0f, 1.0f);

	const float omega = std::acos(dot);

	if (std::abs(omega) < EPSILON) {
		cosResult = nCosOrig;
		sinResult = nSinOrig;
		return;
	}

	const float sinOmega = std::sin(omega);
	if (std::abs(sinOmega) < EPSILON) {
		cosResult = (1.0f - weight) * nCosOrig + weight * nCosRecon;
		sinResult = (1.0f - weight) * nSinOrig + weight * nSinRecon;
		const float len = std::sqrt(cosResult * cosResult + sinResult * sinResult);
		if (len > EPSILON) {
			cosResult /= len;
			sinResult /= len;
		}
		return;
	}

	const float coeffOrig = std::sin((1.0f - weight) * omega) / sinOmega;
	const float coeffRecon = std::sin(weight * omega) / sinOmega;

	cosResult = coeffOrig * nCosOrig + coeffRecon * nCosRecon;
	sinResult = coeffOrig * nSinOrig + coeffRecon * nSinRecon;
}
}

void interpolateBoundaryPhase(std::vector<float>& phases,
							  const std::vector<float>& originalPhases,
							  const std::vector<float>& reconstructedPhases,
							  const std::vector<float>& transitionWeights,
							  size_t binCount) {
	if (phases.size() != binCount ||
		originalPhases.size() != binCount ||
		reconstructedPhases.size() != binCount ||
		transitionWeights.size() != binCount) {
		return;
	}

	for (size_t bin = 0; bin < binCount; ++bin) {
		const float weight = std::clamp(transitionWeights[bin], 0.0f, 1.0f);

		if (weight < EPSILON) {
			phases[bin] = originalPhases[bin];
			continue;
		}

		if (weight > 1.0f - EPSILON) {
			phases[bin] = reconstructedPhases[bin];
			continue;
		}

		const float cosOrig = std::cos(originalPhases[bin]);
		const float sinOrig = std::sin(originalPhases[bin]);
		const float cosRecon = std::cos(reconstructedPhases[bin]);
		const float sinRecon = std::sin(reconstructedPhases[bin]);

		float cosResult, sinResult;
		slerpPhaseVectors(cosOrig, sinOrig, cosRecon, sinRecon, weight,
						  cosResult, sinResult);

		phases[bin] = std::atan2(sinResult, cosResult);
	}
}

void applyTemporalPhaseCoherence(std::vector<std::vector<float>>& allPhases,
								 const std::vector<float>& transitionWeights,
								 size_t width,
								 size_t height,
								 float coherenceFactor) {
	if (allPhases.empty() || width == 0 || height == 0) {
		return;
	}

	const size_t numFrames = allPhases.size();
	const size_t binCount = allPhases[0].size();

	if (transitionWeights.size() != width * height) {
		return;
	}

	constexpr size_t TEMPORAL_WINDOW = 3;

	for (size_t frame = 1; frame < numFrames; ++frame) {
		for (size_t bin = 0; bin < binCount; ++bin) {
			const size_t pixelX = frame;
			const size_t pixelY = bin;

			if (pixelX >= width || pixelY >= height) {
				continue;
			}

			const size_t weightIdx = pixelY * width + pixelX;
			const float boundaryWeight = transitionWeights[weightIdx];

			if (boundaryWeight < 0.05f || boundaryWeight > 0.95f) {
				continue;
			}

			float phaseSum = 0.0f;
			float weightSum = 0.0f;

			for (size_t t = 0; t < TEMPORAL_WINDOW && frame >= t; ++t) {
				const size_t pastFrame = frame - t;
				const float temporalWeight = 1.0f / static_cast<float>(t + 1);

				const float pastPhase = allPhases[pastFrame][bin];
				const float phaseDiff = wrapToPi(allPhases[frame][bin] - pastPhase);

				phaseSum += temporalWeight * phaseDiff;
				weightSum += temporalWeight;
			}

			if (weightSum > EPSILON) {
				const float avgPhaseDiff = phaseSum / weightSum;
				const float expectedPhase = wrapToPi(allPhases[frame - 1][bin] + avgPhaseDiff);

				const float currentPhase = allPhases[frame][bin];
				const float blendWeight = boundaryWeight * coherenceFactor;

				const float cosExp = std::cos(expectedPhase);
				const float sinExp = std::sin(expectedPhase);
				const float cosCur = std::cos(currentPhase);
				const float sinCur = std::sin(currentPhase);

				float cosResult, sinResult;
				slerpPhaseVectors(cosCur, sinCur, cosExp, sinExp, blendWeight,
								  cosResult, sinResult);

				allPhases[frame][bin] = std::atan2(sinResult, cosResult);
			}
		}
	}
}

}

#include "pghi.h"
#include "phase_wrapping.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace PhaseReconstruction {

namespace {
constexpr float EPSILON = 1e-6f;
constexpr float MIN_BIN_INTENSITY = 1e-6f;
constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
}

void reconstructPhasePGHI(const std::vector<std::vector<float>>& allMagnitudes,
					  const std::vector<std::vector<float>>& allFrequencies,
					  size_t currentFrame,
					  std::vector<float>& phases,
					  float sampleRate,
					  int hopSize,
					  const std::vector<float>* prevOutputPhase) {
	const size_t numBins = phases.size();
	if (numBins == 0 || allMagnitudes.empty() || currentFrame >= allMagnitudes.size()) {
		return;
	}

	const float fftSize = static_cast<float>((numBins - 1) * 2);
	const auto& magnitudes = allMagnitudes[currentFrame];
	const std::vector<float>* frequencyRow = currentFrame < allFrequencies.size()
		? &allFrequencies[currentFrame]
		: nullptr;

	std::vector<bool> visited(numBins, false);
	std::vector<std::pair<float, size_t>> heap;

	for (size_t bin = 0; bin < numBins; ++bin) {
		if (magnitudes[bin] > MIN_BIN_INTENSITY) {
			heap.push_back({magnitudes[bin], bin});
		} else {
			phases[bin] = 0.0f;
			visited[bin] = true;
		}
	}

	std::make_heap(heap.begin(), heap.end());

	if (heap.empty()) {
		return;
	}

	std::pop_heap(heap.begin(), heap.end());
	const size_t seedBin = heap.back().second;
	heap.pop_back();

	const float freqResolution = sampleRate / fftSize;
	const float binPhaseAdvance = TWO_PI * freqResolution * static_cast<float>(hopSize) / sampleRate;

	float seedFrequency = (frequencyRow && seedBin < frequencyRow->size() && std::isfinite((*frequencyRow)[seedBin]) && (*frequencyRow)[seedBin] > 0.0f)
		? (*frequencyRow)[seedBin]
		: freqResolution * static_cast<float>(seedBin);
	const float seedAdvance = TWO_PI * seedFrequency * static_cast<float>(hopSize) / sampleRate;

	if (prevOutputPhase && seedBin < prevOutputPhase->size()) {
		phases[seedBin] = wrapToPi((*prevOutputPhase)[seedBin] + seedAdvance);
	} else {
		phases[seedBin] = binPhaseAdvance * static_cast<float>(seedBin);
	}
	visited[seedBin] = true;

	std::vector<size_t> integrationQueue;
	integrationQueue.push_back(seedBin);
	size_t queuePos = 0;

	auto alignToPrevious = [&](size_t binIndex) {
		if (!prevOutputPhase || binIndex >= prevOutputPhase->size()) {
			return;
		}
		const float prevPhase = (*prevOutputPhase)[binIndex];
		const float diff = wrapToPi(phases[binIndex] - prevPhase);
		phases[binIndex] = wrapToPi(prevPhase + diff);
	};

	// Frequency-domain phase gradient integration
	// Průša et al. (2017): phase gradient = 0.5 * d(log magnitude)/d(frequency)
	while (queuePos < integrationQueue.size()) {
		const size_t currentBin = integrationQueue[queuePos++];

		if (currentBin > 0 && !visited[currentBin - 1] && magnitudes[currentBin - 1] > MIN_BIN_INTENSITY) {
			const float logMagCurrent = std::log(std::max(magnitudes[currentBin], EPSILON));
			const float logMagPrev = std::log(std::max(magnitudes[currentBin - 1], EPSILON));
			const float phaseGradient = 0.5f * (logMagCurrent - logMagPrev);

			phases[currentBin - 1] = wrapToPi(phases[currentBin] - phaseGradient - binPhaseAdvance);
			visited[currentBin - 1] = true;
			alignToPrevious(currentBin - 1);
			integrationQueue.push_back(currentBin - 1);
		}

		if (currentBin + 1 < numBins && !visited[currentBin + 1] && magnitudes[currentBin + 1] > MIN_BIN_INTENSITY) {
			const float logMagCurrent = std::log(std::max(magnitudes[currentBin], EPSILON));
			const float logMagNext = std::log(std::max(magnitudes[currentBin + 1], EPSILON));
			const float phaseGradient = 0.5f * (logMagNext - logMagCurrent);

			phases[currentBin + 1] = wrapToPi(phases[currentBin] + phaseGradient + binPhaseAdvance);
			visited[currentBin + 1] = true;
			alignToPrevious(currentBin + 1);
			integrationQueue.push_back(currentBin + 1);
		}
	}

	// Temporal refinement using time-domain gradient
	// Empirically tuned: 0.5× scaling factor provides stable phase evolution
	if (currentFrame > 0) {
		const auto& prevMagnitudes = allMagnitudes[currentFrame - 1];
		for (size_t bin = 0; bin < numBins; ++bin) {
			if (magnitudes[bin] > MIN_BIN_INTENSITY && prevMagnitudes[bin] > MIN_BIN_INTENSITY) {
				const float logMagCurrent = std::log(std::max(magnitudes[bin], EPSILON));
				const float logMagPrev = std::log(std::max(prevMagnitudes[bin], EPSILON));
				const float timeGradient = (logMagCurrent - logMagPrev);

				phases[bin] = wrapToPi(phases[bin] + 0.5f * timeGradient);
			}
		}
	}
}

}

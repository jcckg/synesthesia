#include "pghi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <queue>
#include <stdexcept>

namespace PhaseReconstruction {
namespace {

constexpr double twoPi = 2.0 * std::numbers::pi;

double coefficientMagnitude(const std::vector<std::vector<float>>& magnitudes,
                            const std::size_t frame, const std::size_t bin) {
    const double value = static_cast<double>(magnitudes[frame][bin]);
    if (!std::isfinite(value) || value <= 0.0) return 0.0;
    return value * (bin == 0 || bin + 1 == magnitudes[frame].size() ? 1.0 : 0.5);
}

double logMagnitude(const std::vector<std::vector<float>>& magnitudes,
                    const std::size_t frame, const std::size_t bin) {
    return std::log(std::max(coefficientMagnitude(magnitudes, frame, bin), 1e-30));
}

float wrapped(const double phase) {
    return static_cast<float>(std::remainder(phase, twoPi));
}

float backgroundPhase(const std::size_t index) {
    std::uint64_t value = static_cast<std::uint64_t>(index) + 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    value ^= value >> 31;
    const double unit = static_cast<double>(value >> 11) * (1.0 / 9007199254740992.0);
    return wrapped(twoPi * unit);
}

}

// https://ltfat.org/notes/ltfatnote040.pdf
PhaseGradient phaseGradient(const std::vector<std::vector<float>>& magnitudes,
                            const std::size_t frame, const std::size_t bin,
                            const std::size_t fftSize, const int hopSize, const double gamma) {
    if (fftSize < 2 || hopSize <= 0 || !std::isfinite(gamma) || gamma <= 0.0 ||
        frame >= magnitudes.size() || magnitudes[frame].size() != fftSize / 2 + 1 || bin >= magnitudes[frame].size()) {
        throw std::invalid_argument("Invalid PGHI gradient dimensions or window parameters");
    }
    const auto previous = frame == 0 ? frame : frame - 1;
    const auto next = std::min(frame + 1, magnitudes.size() - 1);
    if (magnitudes[previous].size() != magnitudes[frame].size() ||
        magnitudes[next].size() != magnitudes[frame].size()) {
        throw std::invalid_argument("Inconsistent PGHI frame dimensions");
    }
    const double timeDerivative = previous == next ? 0.0 :
        (logMagnitude(magnitudes, next, bin) - logMagnitude(magnitudes, previous, bin)) /
            static_cast<double>(next - previous);
    const double frequencyDerivative = bin == 0 || bin + 1 == magnitudes[frame].size() ? 0.0 :
        0.5 * (logMagnitude(magnitudes, frame, bin + 1) - logMagnitude(magnitudes, frame, bin - 1));
    const double size = static_cast<double>(fftSize);
    const double hop = static_cast<double>(hopSize);
    const double centre = 0.5 * (size - 1.0);
    return {-gamma / (hop * size) * timeDerivative - twoPi * centre / size,
        hop * size / gamma * frequencyDerivative + twoPi * hop * static_cast<double>(bin) / size};
}

// https://ltfat.org/notes/ltfatnote040.pdf
// https://raw.githubusercontent.com/ltfat/phaseret/master/gabor/pghi_findgamma.m
std::vector<std::vector<float>> reconstructPhasePGHI(
    const std::vector<std::vector<float>>& magnitudes,
    const std::vector<std::vector<float>>& suppliedPhases,
    const std::vector<std::vector<bool>>& knownPhase,
    const std::size_t fftSize, const int hopSize, double gamma, const double relativeTolerance) {
    if (magnitudes.empty()) return {};
    const std::size_t frames = magnitudes.size();
    const std::size_t bins = fftSize / 2 + 1;
    if (fftSize < 2 || fftSize % 2 != 0 || hopSize <= 0 || suppliedPhases.size() != frames ||
        knownPhase.size() != frames || !std::isfinite(relativeTolerance) || relativeTolerance <= 0.0 || relativeTolerance >= 1.0) {
        throw std::invalid_argument("Invalid PGHI dimensions or tolerance");
    }
    if (gamma == 0.0) {
        const double support = static_cast<double>(fftSize - 1);
        gamma = 0.25645 * support * support;
    }
    if (!std::isfinite(gamma) || gamma <= 0.0) throw std::invalid_argument("Invalid PGHI window width");
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (magnitudes[frame].size() != bins || suppliedPhases[frame].size() != bins || knownPhase[frame].size() != bins) {
            throw std::invalid_argument("Inconsistent PGHI frame dimensions");
        }
    }
    auto phases = suppliedPhases;
    std::vector<std::uint8_t> unknown(frames * bins, 0);
    double peak = 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t bin = 0; bin < bins; ++bin) peak = std::max(peak, coefficientMagnitude(magnitudes, frame, bin));
    }
    using Node = std::pair<double, std::size_t>;
    std::priority_queue<Node> heap;
    std::vector<Node> seeds;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t bin = 0; bin < bins; ++bin) {
            const auto index = frame * bins + bin;
            const bool reliable = knownPhase[frame][bin] && std::isfinite(phases[frame][bin]);
            const double magnitude = coefficientMagnitude(magnitudes, frame, bin);
            if (!reliable) {
                phases[frame][bin] = magnitude == 0.0 ? 0.0f : backgroundPhase(index);
                if (magnitude > relativeTolerance * peak) {
                    unknown[index] = 1;
                    seeds.emplace_back(magnitude, index);
                }
            }
        }
    }
    auto neighbours = [frames, bins](const std::size_t index) {
        const std::size_t frame = index / bins, bin = index % bins;
        const auto missing = frames * bins;
        return std::array<std::size_t, 4>{bin + 1 < bins ? index + 1 : missing,
            bin > 0 ? index - 1 : missing, frame + 1 < frames ? index + bins : missing,
            frame > 0 ? index - bins : missing};
    };
    for (std::size_t index = 0; index < unknown.size(); ++index) {
        if (unknown[index] || !knownPhase[index / bins][index % bins] || !std::isfinite(phases[index / bins][index % bins]) ||
            coefficientMagnitude(magnitudes, index / bins, index % bins) <= relativeTolerance * peak) continue;
        for (const auto neighbour : neighbours(index)) {
            if (neighbour < unknown.size() && unknown[neighbour]) {
                heap.emplace(coefficientMagnitude(magnitudes, index / bins, index % bins), index);
                break;
            }
        }
    }
    std::sort(seeds.begin(), seeds.end(), std::greater<Node>());
    std::size_t seedPosition = 0;
    for (;;) {
        while (heap.empty() && seedPosition < seeds.size()) {
            const auto [magnitude, index] = seeds[seedPosition++];
            if (!unknown[index]) continue;
            unknown[index] = 0;
            phases[index / bins][index % bins] = 0.0f;
            heap.emplace(magnitude, index);
        }
        if (heap.empty()) break;
        const auto index = heap.top().second;
        heap.pop();
        const auto frame = index / bins, bin = index % bins;
        const auto gradient = phaseGradient(magnitudes, frame, bin, fftSize, hopSize, gamma);
        for (const auto neighbour : neighbours(index)) {
            if (neighbour >= unknown.size() || !unknown[neighbour]) continue;
            const auto otherFrame = neighbour / bins, otherBin = neighbour % bins;
            const auto otherGradient = phaseGradient(magnitudes, otherFrame, otherBin, fftSize, hopSize, gamma);
            const double step = otherFrame == frame
                ? (otherBin > bin ? 0.5 : -0.5) * (gradient.frequency + otherGradient.frequency)
                : (otherFrame > frame ? 0.5 : -0.5) * (gradient.time + otherGradient.time);
            phases[otherFrame][otherBin] = wrapped(static_cast<double>(phases[frame][bin]) + step);
            unknown[neighbour] = 0;
            heap.emplace(coefficientMagnitude(magnitudes, otherFrame, otherBin), neighbour);
        }
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (const std::size_t bin : {std::size_t{0}, bins - 1}) {
            if (!knownPhase[frame][bin] || !std::isfinite(suppliedPhases[frame][bin])) phases[frame][bin] = std::cos(phases[frame][bin]) < 0.0f
                ? std::numbers::pi_v<float> : 0.0f;
        }
    }
    return phases;
}

}

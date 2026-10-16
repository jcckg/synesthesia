#pragma once

#include <cstddef>
#include <vector>

namespace PhaseReconstruction {

struct PhaseGradient {
    double frequency = 0.0;
    double time = 0.0;
};

// https://ltfat.org/notes/ltfatnote040.pdf
PhaseGradient phaseGradient(const std::vector<std::vector<float>>& magnitudes,
                            std::size_t frame, std::size_t bin,
                            std::size_t fftSize, int hopSize, double gamma);

std::vector<std::vector<float>> reconstructPhasePGHI(
    const std::vector<std::vector<float>>& magnitudes,
    const std::vector<std::vector<float>>& suppliedPhases,
    const std::vector<std::vector<bool>>& knownPhase,
    std::size_t fftSize, int hopSize,
    double gamma = 0.0, double relativeTolerance = 1e-6);

}

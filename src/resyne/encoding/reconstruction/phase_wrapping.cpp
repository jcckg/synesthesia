#include "phase_wrapping.h"

#include <cmath>
#include <numbers>

namespace PhaseReconstruction {

namespace {
constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
}

float wrapToPi(float value) {
	float wrapped = std::fmod(value + std::numbers::pi_v<float>, TWO_PI);
	if (wrapped < 0.0f) {
		wrapped += TWO_PI;
	}
	return wrapped - std::numbers::pi_v<float>;
}

}

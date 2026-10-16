#pragma once

enum class ColourSpace {
	Rec2020,
	DisplayP3,
	SRGB
};

namespace synesthesia {
namespace constants {

constexpr float LAB_EPSILON = 216.0f / 24389.0f;
constexpr float LAB_KAPPA = 24389.0f / 27.0f;
constexpr float LAB_DELTA = 6.0f / 29.0f;

constexpr float MIN_AUDIO_FREQ = 20.0f;
constexpr float MAX_AUDIO_FREQ = 20000.0f;
constexpr float MIN_WAVELENGTH_NM = 390.0f;
constexpr float MAX_WAVELENGTH_NM = 830.0f;

constexpr float HK_ADAPTING_LUMINANCE = 100.0f;

}
}

#pragma once

enum class ColourSpace {
	Rec2020,
	DisplayP3,
	SRGB
};

namespace synesthesia {
namespace constants {

constexpr float CIE_D65_REF_X = 0.94755f;
constexpr float CIE_D65_REF_Y = 1.0f;
constexpr float CIE_D65_REF_Z = 1.07468f;

constexpr float LAB_EPSILON = 0.008856f;
constexpr float LAB_KAPPA = 903.3f;
constexpr float LAB_DELTA = 6.0f / 29.0f;

constexpr float MIN_AUDIO_FREQ = 20.0f;
constexpr float MAX_AUDIO_FREQ = 20000.0f;
constexpr float MIN_WAVELENGTH_NM = 390.0f;
constexpr float MAX_WAVELENGTH_NM = 830.0f;
constexpr float MAX_USABLE_WAVELENGTH_NM = 700.0f;

}
}

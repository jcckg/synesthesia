#pragma once

enum class ColourSpace {
	Rec2020,
	DisplayP3,
	SRGB
};

namespace synesthesia {
namespace constants {

constexpr float CIE_D65_REF_X = 0.9475536f;
constexpr float CIE_D65_REF_Y = 1.0f;
constexpr float CIE_D65_REF_Z = 1.0754043f;

constexpr float LAB_EPSILON = 0.008856f;
constexpr float LAB_KAPPA = 903.3f;
constexpr float LAB_DELTA = 6.0f / 29.0f;

constexpr float MIN_AUDIO_FREQ = 20.0f;
constexpr float MAX_AUDIO_FREQ = 20000.0f;
constexpr float MIN_WAVELENGTH_NM = 390.0f;
constexpr float MAX_WAVELENGTH_NM = 830.0f;
constexpr float MAX_USABLE_WAVELENGTH_NM = 700.0f;
constexpr float REFERENCE_SPL_AT_0_LUFS = 83.0f;
constexpr float REFERENCE_WHITE_LUMINANCE_CDM2 = 100.0f;

// Nayatani (1997) Helmholtz-Kohlrausch VCC correction constants
// CIE D65 white point in CIE 1976 u'v' UCS coordinates
// u' = 4x / (-2x + 12y + 3), v' = 9y / (-2x + 12y + 3) where x=0.31271, y=0.32902
constexpr float CIE_D65_U_PRIME = 0.19767708f;
constexpr float CIE_D65_V_PRIME = 0.46939135f;
constexpr float HK_ADAPTING_LUMINANCE = 100.0f;

}
}

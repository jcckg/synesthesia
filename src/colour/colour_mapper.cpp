#include "colour_mapper.h"
#include "spectral_processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <vector>

namespace {

using ColourSpace = ColourMapper::ColourSpace;

enum class TransferFunction {
	SRGB,
	Rec2020
};

struct ColourSpaceDefinition {
	std::array<float, 9> xyzToRgb;
	std::array<float, 9> rgbToXyz;
	TransferFunction transfer;
};

constexpr ColourSpaceDefinition REC2020_DEFINITION{
	.xyzToRgb = {1.7166512f, -0.35567078f, -0.25336629f,
				 -0.66668433f, 1.6164813f, 0.01576854f,
				 0.01763986f, -0.04277061f, 0.94210315f},
	.rgbToXyz = {0.63695806f, 0.1446169f, 0.16888098f,
				 0.2627002f, 0.67799807f, 0.05930172f,
				 0.0f, 0.02807269f, 1.0609851f},
	.transfer = TransferFunction::Rec2020
};

constexpr ColourSpaceDefinition DISPLAY_P3_DEFINITION{
	.xyzToRgb = {2.4934969f, -0.93138361f, -0.40271077f,
				 -0.82948899f, 1.7626641f, 0.02362468f,
				 0.03584583f, -0.07617239f, 0.9568845f},
	.rgbToXyz = {0.48657095f, 0.2656677f, 0.19821729f,
				 0.22897457f, 0.69173855f, 0.07928691f,
				 0.0f, 0.04511338f, 1.0439444f},
	.transfer = TransferFunction::SRGB
};

constexpr ColourSpaceDefinition SRGB_DEFINITION{
	.xyzToRgb = {3.2409699f, -1.5373832f, -0.49861076f,
				 -0.96924365f, 1.8759675f, 0.04155506f,
				 0.05563008f, -0.20397697f, 1.0569714f},
	.rgbToXyz = {0.4123908f, 0.35758433f, 0.18048079f,
				 0.212639f, 0.7151687f, 0.07219231f,
				 0.01933082f, 0.11919478f, 0.95053214f},
	.transfer = TransferFunction::SRGB
};

const ColourSpaceDefinition& getDefinition(const ColourSpace colourSpace) {
	switch (colourSpace) {
		case ColourSpace::Rec2020:
			return REC2020_DEFINITION;
		case ColourSpace::DisplayP3:
			return DISPLAY_P3_DEFINITION;
		case ColourSpace::SRGB:
		default:
			return SRGB_DEFINITION;
	}
}

std::array<float, 3> multiplyMatrix(const std::array<float, 9>& matrix,
									const float x, const float y, const float z) {
	return {
		matrix[0] * x + matrix[1] * y + matrix[2] * z,
		matrix[3] * x + matrix[4] * y + matrix[5] * z,
		matrix[6] * x + matrix[7] * y + matrix[8] * z
	};
}

void mixTowardsWhite(float& r, float& g, float& b) {
	float mixFactor = 1.0f;
	if (r < 0.0f) {
		mixFactor = std::min(mixFactor, 1.0f / (1.0f - r));
	}
	if (g < 0.0f) {
		mixFactor = std::min(mixFactor, 1.0f / (1.0f - g));
	}
	if (b < 0.0f) {
		mixFactor = std::min(mixFactor, 1.0f / (1.0f - b));
	}

	if (mixFactor < 1.0f) {
		const auto mixChannel = [mixFactor](const float channel) {
			return 1.0f + mixFactor * (channel - 1.0f);
		};

		r = mixChannel(r);
		g = mixChannel(g);
		b = mixChannel(b);
	}
}

float encodeSRGB(const float c) {
	const float absValue = std::abs(c);
	if (absValue <= ColourMapper::SRGB_GAMMA_ENCODE_THRESHOLD) {
		return 12.92f * c;
	}

	const float encoded = 1.055f * std::pow(absValue, 1.0f / 2.4f) - 0.055f;
	return std::copysign(encoded, c);
}

float decodeSRGB(const float c) {
	const float absValue = std::abs(c);
	if (absValue <= ColourMapper::SRGB_GAMMA_DECODE_THRESHOLD) {
		return c / 12.92f;
	}

	const float decoded = std::pow((absValue + 0.055f) / 1.055f, 2.4f);
	return std::copysign(decoded, c);
}

float encodeRec2020(const float c) {
	constexpr float alpha = 1.0992968f;
	constexpr float beta = 0.01805397f;

	const float absValue = std::abs(c);
	if (absValue < beta) {
		return 4.5f * c;
	}

	const float encoded = alpha * std::pow(absValue, 0.45f) - (alpha - 1.0f);
	return std::copysign(encoded, c);
}

float decodeRec2020(const float c) {
	constexpr float alpha = 1.0992968f;
	constexpr float beta = 0.01805397f;

	const float absValue = std::abs(c);
	if (absValue < 4.5f * beta) {
		return c / 4.5f;
	}

	const float decoded = std::pow((absValue + (alpha - 1.0f)) / alpha, 1.0f / 0.45f);
	return std::copysign(decoded, c);
}

float encodeValue(const TransferFunction transfer, const float value) {
	switch (transfer) {
		case TransferFunction::Rec2020:
			return encodeRec2020(value);
		case TransferFunction::SRGB:
		default:
			return encodeSRGB(value);
	}
}

float decodeValue(const TransferFunction transfer, const float value) {
	switch (transfer) {
		case TransferFunction::Rec2020:
			return decodeRec2020(value);
		case TransferFunction::SRGB:
		default:
			return decodeSRGB(value);
	}
}

}

void ColourMapper::interpolateCIE(float wavelength, float& X, float& Y, float& Z) {
	const float tableMin = CIE_2006.front()[0];
	const float tableMax = CIE_2006.back()[0];

	if (wavelength > tableMax) {
		const auto& lastEntry = CIE_2006[CIE_TABLE_SIZE - 1];
		X = lastEntry[1] * 0.1f * SUB_AUDIO_BRIGHTNESS_BOOST;
		Y = lastEntry[2] * 0.1f * SUB_AUDIO_BRIGHTNESS_BOOST;
		Z = lastEntry[3] * 0.1f * SUB_AUDIO_BRIGHTNESS_BOOST;
		return;
	}

	if (!std::isfinite(wavelength)) {
		wavelength = tableMin;
	}

	const float clamped = std::clamp(wavelength, tableMin, tableMax);
	const float indexFloat = clamped - tableMin;
	size_t index = static_cast<size_t>(std::floor(indexFloat));

	if (index >= CIE_TABLE_SIZE - 1) {
		index = CIE_TABLE_SIZE - 2;
	}

	const auto& entry0 = CIE_2006[index];
	const auto& entry1 = CIE_2006[index + 1];

	const float lambda0 = entry0[0];
	const float lambda1 = entry1[0];

	float t = 0.0f;
	if (lambda1 - lambda0 > EPSILON_SMALL) {
		t = (clamped - lambda0) / (lambda1 - lambda0);
		t = std::clamp(t, 0.0f, 1.0f);
	}

	X = std::lerp(entry0[1], entry1[1], t);
	Y = std::lerp(entry0[2], entry1[2], t);
	Z = std::lerp(entry0[3], entry1[3], t);
}

void ColourMapper::XYZtoRGB(const float X, const float Y, const float Z, float& r, float& g,
							float& b, const ColourSpace colourSpace, const bool applyGamma,
							const bool applyGamutMapping) {
	const auto& definition = getDefinition(colourSpace);
	const auto linearRgb = multiplyMatrix(definition.xyzToRgb, X, Y, Z);

	float outR = linearRgb[0];
	float outG = linearRgb[1];
	float outB = linearRgb[2];

	if (applyGamutMapping) {
		mixTowardsWhite(outR, outG, outB);
		gamutMapRGB(outR, outG, outB);
		outR = std::clamp(outR, 0.0f, 1.0f);
		outG = std::clamp(outG, 0.0f, 1.0f);
		outB = std::clamp(outB, 0.0f, 1.0f);
	}

	if (applyGamma) {
		outR = encodeValue(definition.transfer, outR);
		outG = encodeValue(definition.transfer, outG);
		outB = encodeValue(definition.transfer, outB);
	}

	if (applyGamutMapping && applyGamma) {
		outR = std::clamp(outR, 0.0f, 1.0f);
		outG = std::clamp(outG, 0.0f, 1.0f);
		outB = std::clamp(outB, 0.0f, 1.0f);
	}

	r = outR;
	g = outG;
	b = outB;
}

void ColourMapper::RGBtoXYZ(const float r, const float g, const float b, float& X, float& Y,
							float& Z, const ColourSpace colourSpace) {
	const auto& definition = getDefinition(colourSpace);

	const float linearR = decodeValue(definition.transfer, r);
	const float linearG = decodeValue(definition.transfer, g);
	const float linearB = decodeValue(definition.transfer, b);

	const auto xyz = multiplyMatrix(definition.rgbToXyz, linearR, linearG, linearB);
	X = xyz[0];
	Y = xyz[1];
	Z = xyz[2];
}

// CIE 15:2004 - Colorimetry, 3rd edition - CIE LAB colour space
// Converts CIE XYZ (D65 illuminant) to CIE 1976 L*a*b* colour space
// L* = lightness (0-100), a* = green-red axis, b* = blue-yellow axis
// Uses published CIE constants: ε = 0.008856, κ = 903.3
// http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_Lab.html
void ColourMapper::XYZtoLab(const float X, const float Y, const float Z, float& L, float& a,
							float& b) {
	const float xr = REF_X > 0.0f ? X / REF_X : 0.0f;
	const float yr = REF_Y > 0.0f ? Y / REF_Y : 0.0f;
	const float zr = REF_Z > 0.0f ? Z / REF_Z : 0.0f;

	// CIE 15:2004 piecewise function with published constants
	auto f = [](const float t) {
		constexpr float epsilon = synesthesia::constants::LAB_EPSILON;
		constexpr float kappa = synesthesia::constants::LAB_KAPPA;
		return t > epsilon ? std::pow(t, 1.0f / 3.0f) : (kappa * t + 16.0f) / 116.0f;
	};

	const float fx = f(xr);
	const float fy = f(yr);
	const float fz = f(zr);

	L = 116.0f * fy - 16.0f;
	a = 500.0f * (fx - fy);
	b = 200.0f * (fy - fz);

	L = std::clamp(L, 0.0f, 100.0f);
	a = std::clamp(a, -128.0f, 127.0f);
	b = std::clamp(b, -128.0f, 127.0f);
}

// CIE 15:2004 - Colorimetry, 3rd edition - CIE LAB colour space
// Converts CIE 1976 L*a*b* colour space to CIE XYZ (D65 illuminant)
// L* = lightness (0-100), a* = green-red axis, b* = blue-yellow axis
// Uses inverse threshold δ = 6/29, where δ³ = ε (epsilon from XYZ to LAB)
// http://www.brucelindbloom.com/index.html?Eqn_Lab_to_XYZ.html
void ColourMapper::LabtoXYZ(float L, float a, float b, float& X, float& Y, float& Z) {
	L = std::clamp(L, 0.0f, 100.0f);
	a = std::clamp(a, -128.0f, 127.0f);
	b = std::clamp(b, -128.0f, 127.0f);

	const float fY = (L + 16.0f) / 116.0f;
	const float fX = fY + a / 500.0f;
	const float fZ = fY - b / 200.0f;

	// Inverse of CIE LAB piecewise function
	auto fInv = [](const float t) {
		constexpr float delta = synesthesia::constants::LAB_DELTA;
		constexpr float delta_squared = delta * delta;
		return t > delta ? std::pow(t, 3.0f) : 3.0f * delta_squared * (t - 4.0f / 29.0f);
	};

	X = REF_X * fInv(fX);
	Y = REF_Y * fInv(fY);
	Z = REF_Z * fInv(fZ);

	X = std::max(0.0f, X);
	Y = std::max(0.0f, Y);
	Z = std::max(0.0f, Z);
}

void ColourMapper::RGBtoLab(const float r, const float g, const float b, float& L, float& a,
							float& b_comp, const ColourSpace colourSpace) {
	float X, Y, Z;
	RGBtoXYZ(r, g, b, X, Y, Z, colourSpace);
	XYZtoLab(X, Y, Z, L, a, b_comp);
}

void ColourMapper::LabtoRGB(const float L, const float a, const float b_comp, float& r, float& g,
							float& b, const ColourSpace colourSpace) {
	float X, Y, Z;
	LabtoXYZ(L, a, b_comp, X, Y, Z);
	XYZtoRGB(X, Y, Z, r, g, b, colourSpace, true, false);
}

// Oklab: Björn Ottosson (2020) - https://bottosson.github.io/posts/oklab/
// Perceptually uniform colour space with better hue linearity than CIELAB
// M1: sRGB to approximate cone responses, M2: after cube root to Lab
void ColourMapper::RGBtoOklab(const float r, const float g, const float b, float& L, float& a,
							  float& b_comp, const ColourSpace colourSpace) {
	const auto& definition = getDefinition(colourSpace);

	const float linearR = decodeValue(definition.transfer, r);
	const float linearG = decodeValue(definition.transfer, g);
	const float linearB = decodeValue(definition.transfer, b);

	const float l = 0.4122214708f * linearR + 0.5363325363f * linearG + 0.0514459929f * linearB;
	const float m = 0.2119034982f * linearR + 0.6806995451f * linearG + 0.1073969566f * linearB;
	const float s = 0.0883024619f * linearR + 0.2817188376f * linearG + 0.6299787005f * linearB;

	const float l_ = std::cbrt(std::max(0.0f, l));
	const float m_ = std::cbrt(std::max(0.0f, m));
	const float s_ = std::cbrt(std::max(0.0f, s));

	L = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
	a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
	b_comp = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;

	L = std::clamp(L * 100.0f, 0.0f, 100.0f);
	a = std::clamp(a * 100.0f, -100.0f, 100.0f);
	b_comp = std::clamp(b_comp * 100.0f, -100.0f, 100.0f);
}

void ColourMapper::OklabtoRGB(const float L, const float a, const float b_comp, float& r, float& g,
							  float& b, const ColourSpace colourSpace) {
	const float L_norm = std::clamp(L / 100.0f, 0.0f, 1.0f);
	const float a_norm = std::clamp(a / 100.0f, -1.0f, 1.0f);
	const float b_norm = std::clamp(b_comp / 100.0f, -1.0f, 1.0f);

	const float l_ = L_norm + 0.3963377774f * a_norm + 0.2158037573f * b_norm;
	const float m_ = L_norm - 0.1055613458f * a_norm - 0.0638541728f * b_norm;
	const float s_ = L_norm - 0.0894841775f * a_norm - 1.2914855480f * b_norm;

	const float l = l_ * l_ * l_;
	const float m = m_ * m_ * m_;
	const float s = s_ * s_ * s_;

	float linearR = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
	float linearG = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
	float linearB = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;

	linearR = std::clamp(linearR, 0.0f, 1.0f);
	linearG = std::clamp(linearG, 0.0f, 1.0f);
	linearB = std::clamp(linearB, 0.0f, 1.0f);

	const auto& definition = getDefinition(colourSpace);
	r = encodeValue(definition.transfer, linearR);
	g = encodeValue(definition.transfer, linearG);
	b = encodeValue(definition.transfer, linearB);
}

void ColourMapper::gamutMapRGB(float& r, float& g, float& b) {
	const bool isOutOfGamut = r < 0.0f || r > 1.0f || g < 0.0f || g > 1.0f || b < 0.0f || b > 1.0f;
	if (!isOutOfGamut) {
		return;
	}

	mixTowardsWhite(r, g, b);

	const float maxComponent = std::max({r, g, b});
	if (maxComponent > 1.0f && maxComponent > 0.0f) {
		const float scale = 1.0f / maxComponent;
		r *= scale;
		g *= scale;
		b *= scale;
	}

	r = std::clamp(r, 0.0f, 1.0f);
	g = std::clamp(g, 0.0f, 1.0f);
	b = std::clamp(b, 0.0f, 1.0f);
}

void ColourMapper::wavelengthToRGBCIE(float wavelength, float& r, float& g, float& b,
									  ColourSpace colourSpace, bool applyGamutMapping) {
	if (!std::isfinite(wavelength)) {
		wavelength = MIN_WAVELENGTH;
	}

	float X, Y, Z;
	interpolateCIE(wavelength, X, Y, Z);

	XYZtoRGB(X, Y, Z, r, g, b, colourSpace, true, applyGamutMapping);
}

float ColourMapper::wavelengthToLogFrequency(const float wavelengthIn) {
	// CIE 2006 2-degree table range: 390-830nm (using 390-830nm for better range of usable colours).
	const float MIN_AUDIBLE_WAVELENGTH = 390.0f;
	const float MAX_AUDIBLE_WAVELENGTH = 700.0f;

	float wavelength = std::isfinite(wavelengthIn) ? wavelengthIn : MAX_AUDIBLE_WAVELENGTH;
	wavelength = std::clamp(wavelength, MIN_AUDIBLE_WAVELENGTH, MAX_AUDIBLE_WAVELENGTH);

	const float range = MAX_AUDIBLE_WAVELENGTH - MIN_AUDIBLE_WAVELENGTH;
	if (range <= 0.0f) {
		return MIN_FREQ;
	}

	const float t = (MAX_AUDIBLE_WAVELENGTH - wavelength) / range;
	const float logFreqMin = std::log2(MIN_FREQ);
	const float logFreqMax = std::log2(MAX_FREQ);
	const float logFreq = logFreqMin + t * (logFreqMax - logFreqMin);

	return std::pow(2.0f, logFreq);
}

// Ray-spectrum locus intersection using parametric line-segment intersection
// Returns the wavelength where a ray from D65 white point through (x,y) intersects the spectrum locus
// Reference: Computational colour science using MATLAB (2nd ed.), Stephen Westland et al.
ColourMapper::RayIntersectionResult ColourMapper::findSpectrumIntersection(
	const float rayDX, const float rayDY, const float whiteX, const float whiteY) {

	RayIntersectionResult result{false, MAX_WAVELENGTH, std::numeric_limits<float>::max()};

	for (size_t i = 0; i < CIE_TABLE_SIZE - 1; ++i) {
		const auto& entry0 = CIE_2006[i];
		const auto& entry1 = CIE_2006[i + 1];

		const float lambda0 = entry0[0];
		const float lambda1 = entry1[0];
		if (lambda1 < MIN_WAVELENGTH || lambda0 > MAX_WAVELENGTH) {
			continue;
		}

		const float specSum0 = entry0[1] + entry0[2] + entry0[3];
		const float specSum1 = entry1[1] + entry1[2] + entry1[3];
		if (specSum0 <= 0.0f || specSum1 <= 0.0f) {
			continue;
		}

		const float x0 = entry0[1] / specSum0;
		const float y0 = entry0[2] / specSum0;
		const float x1 = entry1[1] / specSum1;
		const float y1 = entry1[2] / specSum1;

		const float segDX = x1 - x0;
		const float segDY = y1 - y0;

		// Solve for ray-segment intersection: ray(t) = white + t*rayD, segment(u) = p0 + u*segD
		const float det = rayDX * (-segDY) - rayDY * (-segDX);
		if (std::abs(det) < EPSILON_TINY) {
			continue;
		}

		const float px = x0 - whiteX;
		const float py = y0 - whiteY;

		const float t = (px * (-segDY) - py * (-segDX)) / det;
		const float u = (rayDX * py - rayDY * px) / det;

		if (t >= 0.0f && u >= 0.0f && u <= 1.0f) {
			if (!result.found || t < result.parameterT) {
				result.found = true;
				result.parameterT = t;
				const float wavelength = std::lerp(lambda0, lambda1, u);
				result.wavelength = std::clamp(wavelength, MIN_WAVELENGTH, MAX_WAVELENGTH);
			}
		}
	}

	return result;
}

// Fallback: find spectral wavelength with best angular alignment and minimum perpendicular distance
// Used when ray doesn't intersect spectrum locus (for desaturated/out-of-gamut colours)
float ColourMapper::findClosestSpectralMatch(
	const float rayDX, const float rayDY, const float whiteX, const float whiteY, const float rayLength) {

	float bestCos = -1.0f;
	float bestDistance = std::numeric_limits<float>::max();
	float bestWavelength = MAX_WAVELENGTH;

	for (const auto& entry : CIE_2006) {
		const float lambda = entry[0];
		if (lambda < MIN_WAVELENGTH || lambda > MAX_WAVELENGTH) {
			continue;
		}

		const float specSum = entry[1] + entry[2] + entry[3];
		if (specSum <= 0.0f) {
			continue;
		}

		const float xSpec = entry[1] / specSum;
		const float ySpec = entry[2] / specSum;

		const float vecSpecX = xSpec - whiteX;
		const float vecSpecY = ySpec - whiteY;
		const float specLen = std::sqrt(vecSpecX * vecSpecX + vecSpecY * vecSpecY);
		if (specLen < EPSILON_SMALL) {
			continue;
		}

		const float dot = rayDX * vecSpecX + rayDY * vecSpecY;
		const float cosAngle = dot / (rayLength * specLen);
		if (!std::isfinite(cosAngle) || cosAngle < 0.0f) {
			continue;
		}

		// Perpendicular distance from ray to spectral point
		const float cross = rayDX * vecSpecY - rayDY * vecSpecX;
		const float distance = std::abs(cross) / specLen;

		// Prefer better angle, break ties with distance
		if (cosAngle > bestCos || (std::abs(cosAngle - bestCos) < 1e-4f && distance < bestDistance)) {
			bestCos = cosAngle;
			bestDistance = distance;
			bestWavelength = lambda;
		}
	}

	return std::clamp(bestWavelength, MIN_WAVELENGTH, MAX_WAVELENGTH);
}

float ColourMapper::XYZtoDominantWavelength(const float X, const float Y, const float Z) {
    const float sum = X + Y + Z;
    if (!std::isfinite(sum) || sum <= 0.0f) {
        return MAX_WAVELENGTH;
    }

    // Convert to CIE xy chromaticity coordinates
    const float x = X / sum;
    const float y = Y / sum;

    // CIE D65 white point in xy chromaticity coordinates
    constexpr float whiteX = 0.31271f;
    constexpr float whiteY = 0.32902f;

    // Ray from white point through the colour's chromaticity
    const float rayDX = x - whiteX;
    const float rayDY = y - whiteY;
    const float rayLenSq = rayDX * rayDX + rayDY * rayDY;

    // Colour is too close to white point (achromatic)
    if (rayLenSq < EPSILON_TINY) {
        return MAX_WAVELENGTH;
    }

    // Try to find where ray intersects the spectrum locus
    const auto intersection = findSpectrumIntersection(rayDX, rayDY, whiteX, whiteY);
    if (intersection.found) {
        return intersection.wavelength;
    }

    // Fallback: find closest spectral wavelength by angular alignment
    const float rayLength = std::sqrt(rayLenSq);
    return findClosestSpectralMatch(rayDX, rayDY, whiteX, whiteY, rayLength);
}

float ColourMapper::logFrequencyToWavelength(const float freq) {
	if (!std::isfinite(freq) || freq <= 0.0f)
		return MAX_WAVELENGTH;

	// CIE 2006 2-degree table range: 390-830nm
	// Practical visible range: 390-700nm
	// The 700nm cutoff avoids the deep red region (700-830nm) where:
	// 1. Cone sensitivity drops rapidly (L-cone peak ~565nm, sensitivity at 700nm ~10%)
	// 2. Colours become increasingly monochromatic/indistinguishable
	// 3. RGB primaries have poor gamut coverage in this region
	// Reference: "The CIE Colorimetric System" - Wyszecki & Stiles, Color Science (2nd ed.)
	const float AUDIBLE_MIN_WAVELENGTH = 390.0f;
	const float AUDIBLE_MAX_WAVELENGTH = 700.0f;

	if (freq < MIN_FREQ) {
		const float SUB_AUDIO_MIN = 0.1f;
		const float t_sub = std::clamp((freq - SUB_AUDIO_MIN) / (MIN_FREQ - SUB_AUDIO_MIN), 0.0f, 1.0f);
		return 900.0f - t_sub * (900.0f - AUDIBLE_MAX_WAVELENGTH);
	}

	const float MIN_LOG_FREQ = std::log2(MIN_FREQ);
	const float MAX_LOG_FREQ = std::log2(MAX_FREQ);
	const float LOG_FREQ_RANGE = MAX_LOG_FREQ - MIN_LOG_FREQ;

	const float logFreq = std::log2(freq);
	const float normalisedLogFreq = (logFreq - MIN_LOG_FREQ) / LOG_FREQ_RANGE;
	const float t = std::clamp(normalisedLogFreq, 0.0f, 1.0f);

	return AUDIBLE_MAX_WAVELENGTH - t * (AUDIBLE_MAX_WAVELENGTH - AUDIBLE_MIN_WAVELENGTH);
}

ColourMapper::SpectralCharacteristics ColourMapper::calculateSpectralCharacteristics(
	const std::vector<float>& spectrum, const float sampleRate) {
	SpectralCharacteristics result{0.5f, 0.0f, 0.0f};

	if (spectrum.empty() || sampleRate <= 0.0f) {
		return result;
	}

	thread_local static std::vector<float> validValues;
	thread_local static std::vector<float> validFrequencies;
	validValues.clear();
	validFrequencies.clear();

	constexpr size_t MAX_REASONABLE_CAPACITY = 8192;
	if (validValues.capacity() > MAX_REASONABLE_CAPACITY) {
		validValues.shrink_to_fit();
	}
	if (validFrequencies.capacity() > MAX_REASONABLE_CAPACITY) {
		validFrequencies.shrink_to_fit();
	}

	validValues.reserve(spectrum.size());
	validFrequencies.reserve(spectrum.size());

	float totalWeight = 0.0f;
	float weightedFreqSum = 0.0f;

	for (size_t i = 0; i < spectrum.size(); ++i) {
		float value = spectrum[i];
		if (value <= EPSILON_SMALL || !std::isfinite(value)) {
			continue;
		}

		const float freq = spectrum.size() <= 1 ? 0.0f :
			static_cast<float>(i) * sampleRate / (2.0f * (spectrum.size() - 1));
		if (freq < MIN_FREQ || freq > MAX_FREQ) {
			continue;
		}

		validValues.push_back(value);
		validFrequencies.push_back(freq);
		totalWeight += value;
		weightedFreqSum += freq * value;
	}

	if (!validValues.empty() && totalWeight > 0.0f) {
		float logSum = 0.0f;
		for (const float value : validValues) {
			logSum += std::log(value);
		}

		const float geometricMean = std::exp(logSum / validValues.size());

		if (const float arithmeticMean = totalWeight / validValues.size();
			arithmeticMean > EPSILON_TINY) {
			result.flatness = geometricMean / arithmeticMean;
		}

		result.centroid = weightedFreqSum / totalWeight;

		float spreadSum = 0.0f;
		for (size_t i = 0; i < validFrequencies.size(); ++i) {
			const float diff = validFrequencies[i] - result.centroid;
			spreadSum += validValues[i] * diff * diff;
		}

		if (totalWeight > 0.0f) {
			result.spread = std::sqrt(spreadSum / totalWeight);
			result.normalisedSpread = std::min(result.spread / SPREAD_NORMALISATION, 1.0f);
		}
	}

	return result;
}

ColourMapper::ColourResult ColourMapper::spectrumToColour(
const std::vector<float>& magnitudes,
const std::vector<float>& phases,
const float sampleRate,
const float gamma,
const ColourSpace colourSpace,
const bool applyGamutMapping
) {
	const auto spectralResult = SpectralProcessor::spectrumToColour(
		magnitudes, phases, sampleRate, gamma, colourSpace, applyGamutMapping
	);

	ColourResult result{};
	result.r = spectralResult.r;
	result.g = spectralResult.g;
	result.b = spectralResult.b;
	result.L = spectralResult.L;
	result.a = spectralResult.a;
	result.b_comp = spectralResult.b_comp;
	result.dominantWavelength = spectralResult.dominantWavelength;
	result.dominantFrequency = spectralResult.dominantFrequency;
	result.colourIntensity = spectralResult.Y;
	result.spectralCentroid = spectralResult.spectralCentroid;
	result.spectralFlatness = spectralResult.spectralFlatness;
	result.spectralSpread = spectralResult.spectralSpread;
	result.spectralRolloff = spectralResult.spectralRolloff;
	result.spectralCrestFactor = spectralResult.spectralCrestFactor;

	return result;
}

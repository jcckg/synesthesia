#include "resyne/conversions/colour_space.h"
#include <cmath>

namespace ReSyne {
namespace Conversions {

void RGBtoXYZ(float r, float g, float b, float& X, float& Y, float& Z,
		   ColourMapper::ColourSpace colourSpace) {
	ColourMapper::RGBtoXYZ(r, g, b, X, Y, Z, colourSpace);
}

void XYZtoLab(float X, float Y, float Z, float& L, float& a, float& b,
		   ColourMapper::ColourSpace) {
	ColourMapper::XYZtoLab(X, Y, Z, L, a, b);
}

void LabtoXYZ(float L, float a, float b, float& X, float& Y, float& Z,
		   ColourMapper::ColourSpace) {
	ColourMapper::LabtoXYZ(L, a, b, X, Y, Z);
}

void XYZtoRGB(float X, float Y, float Z, float& r, float& g, float& b,
		   ColourMapper::ColourSpace colourSpace) {
	ColourMapper::XYZtoRGB(X, Y, Z, r, g, b, colourSpace, true, true);
}

void RGBtoLab(float r, float g, float b, float& L, float& a, float& b_comp,
		   ColourMapper::ColourSpace colourSpace) {
	ColourMapper::RGBtoLab(r, g, b, L, a, b_comp, colourSpace);
}

void LabtoRGB(float L, float a, float b_comp, float& r, float& g, float& b,
		   ColourMapper::ColourSpace colourSpace) {
	ColourMapper::LabtoRGB(L, a, b_comp, r, g, b, colourSpace);
}

}
}

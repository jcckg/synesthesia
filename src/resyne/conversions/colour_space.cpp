#include "resyne/conversions/colour_space.h"
#include <cmath>

namespace ReSyne {
namespace Conversions {

void RGBtoXYZ(float r, float g, float b, float& X, float& Y, float& Z,
		   ColourCore::ColourSpace colourSpace) {
	ColourCore::RGBtoXYZ(r, g, b, X, Y, Z, colourSpace);
}

void XYZtoLab(float X, float Y, float Z, float& L, float& a, float& b,
		   ColourCore::ColourSpace) {
	ColourCore::XYZtoLab(X, Y, Z, L, a, b);
}

void LabtoXYZ(float L, float a, float b, float& X, float& Y, float& Z,
		   ColourCore::ColourSpace) {
	ColourCore::LabtoXYZ(L, a, b, X, Y, Z);
}

void XYZtoRGB(float X, float Y, float Z, float& r, float& g, float& b,
		   ColourCore::ColourSpace colourSpace) {
	ColourCore::XYZtoRGB(X, Y, Z, r, g, b, colourSpace, true, true);
}

void RGBtoLab(float r, float g, float b, float& L, float& a, float& b_comp,
		   ColourCore::ColourSpace colourSpace) {
	ColourCore::RGBtoLab(r, g, b, L, a, b_comp, colourSpace);
}

void LabtoRGB(float L, float a, float b_comp, float& r, float& g, float& b,
		   ColourCore::ColourSpace colourSpace) {
	ColourCore::LabtoRGB(L, a, b_comp, r, g, b, colourSpace, true);
}

}
}

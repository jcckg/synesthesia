#pragma once

#include "colour/colour_core.h"

namespace ReSyne {
namespace Conversions {

void RGBtoLab(float r, float g, float b, float& L, float& a, float& b_comp,
              ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020);
void LabtoRGB(float L, float a, float b_comp, float& r, float& g, float& b,
              ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020);

void RGBtoXYZ(float r, float g, float b, float& X, float& Y, float& Z,
              ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020);
void XYZtoRGB(float X, float Y, float Z, float& r, float& g, float& b,
              ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020);

void XYZtoLab(float X, float Y, float Z, float& L, float& a, float& b,
              ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020);
void LabtoXYZ(float L, float a, float b, float& X, float& Y, float& Z,
              ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020);

}
}

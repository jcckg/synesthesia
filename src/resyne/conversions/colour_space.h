#pragma once

#include "colour/colour_mapper.h"

namespace ReSyne {
namespace Conversions {

void RGBtoLab(float r, float g, float b, float& L, float& a, float& b_comp,
              ColourMapper::ColourSpace colourSpace = ColourMapper::ColourSpace::Rec2020);
void LabtoRGB(float L, float a, float b_comp, float& r, float& g, float& b,
              ColourMapper::ColourSpace colourSpace = ColourMapper::ColourSpace::Rec2020);

void RGBtoXYZ(float r, float g, float b, float& X, float& Y, float& Z,
              ColourMapper::ColourSpace colourSpace = ColourMapper::ColourSpace::Rec2020);
void XYZtoRGB(float X, float Y, float Z, float& r, float& g, float& b,
              ColourMapper::ColourSpace colourSpace = ColourMapper::ColourSpace::Rec2020);

void XYZtoLab(float X, float Y, float Z, float& L, float& a, float& b,
              ColourMapper::ColourSpace colourSpace = ColourMapper::ColourSpace::Rec2020);
void LabtoXYZ(float L, float a, float b, float& X, float& Y, float& Z,
              ColourMapper::ColourSpace colourSpace = ColourMapper::ColourSpace::Rec2020);

}
}

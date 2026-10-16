#pragma once

#include "colour/colour_core.h"

namespace ColourPresentation {

void convertToSRGB(float& r, float& g, float& b, ColourCore::ColourSpace sourceSpace);

float sanitiseUnitValue(float value);

void applyOutputPrecision(float& r,
                          float& g,
                          float& b);

void applyOutputPrecision(float& r,
                          float& g,
                          float& b,
                          float& a);

}

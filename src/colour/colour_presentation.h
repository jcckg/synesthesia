#pragma once

namespace ColourPresentation {

float sanitiseUnitValue(float value);

void applyOutputPrecision(float& r,
                          float& g,
                          float& b);

void applyOutputPrecision(float& r,
                          float& g,
                          float& b,
                          float& a);

}

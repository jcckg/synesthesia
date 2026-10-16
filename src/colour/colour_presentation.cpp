#include "colour/colour_presentation.h"

#include <algorithm>
#include <cmath>

namespace ColourPresentation {

// https://www.w3.org/TR/css-color-4/#color-conversion-code
void convertToSRGB(float& r, float& g, float& b, const ColourCore::ColourSpace sourceSpace) {
    if (sourceSpace != ColourCore::ColourSpace::SRGB) {
        float X, Y, Z;
        ColourCore::RGBtoXYZ(r, g, b, X, Y, Z, sourceSpace);
        ColourCore::XYZtoRGB(X, Y, Z, r, g, b, ColourCore::ColourSpace::SRGB, true, true);
    }
    applyOutputPrecision(r, g, b);
}

float sanitiseUnitValue(const float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }

    return std::clamp(value, 0.0f, 1.0f);
}

void applyOutputPrecision(float& r,
                          float& g,
                          float& b) {
    r = sanitiseUnitValue(r);
    g = sanitiseUnitValue(g);
    b = sanitiseUnitValue(b);
}

void applyOutputPrecision(float& r,
                          float& g,
                          float& b,
                          float& a) {
    applyOutputPrecision(r, g, b);
    a = sanitiseUnitValue(a);
}

}

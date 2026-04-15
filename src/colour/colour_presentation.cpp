#include "colour/colour_presentation.h"

#include <algorithm>
#include <cmath>

namespace ColourPresentation {

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

#pragma once

#include <string>

namespace ReSyne::Timeline::Labels {

double chooseMajorTickStep(double durationSeconds, double maximumTicks = 8.0);
int chooseMinorTickDivisions(double majorStep, double majorSpacing);
std::string formatTickLabel(double seconds, double majorStep, bool showHours = false);

}

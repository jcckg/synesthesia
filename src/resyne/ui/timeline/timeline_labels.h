#pragma once

#include <string>

namespace ReSyne::Timeline::Labels {

double chooseMajorTickStep(double durationSeconds);
std::string formatTickLabel(double seconds, double majorStep);

}

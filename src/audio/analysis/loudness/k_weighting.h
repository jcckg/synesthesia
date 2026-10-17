#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <numbers>

namespace AudioAnalysis {

struct KWeightingSection {
    std::array<double, 3> b;
    std::array<double, 3> a;
};

// https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf
inline std::array<KWeightingSection, 2> kWeighting(double sampleRate) {
    const double k = std::tan(std::numbers::pi * 1681.974450955533 / sampleRate);
    const double q = .7071752369554196;
    const double vh = std::pow(10.0, 3.999843853973347 / 20.0);
    const double vb = std::pow(vh, .4996667741545416);
    const double a0 = 1.0 + k / q + k * k;
    const double hp = std::tan(std::numbers::pi * 38.13547087602444 / sampleRate);
    const double hpQ = .5003270373238773;
    const double hpA0 = 1.0 + hp / hpQ + hp * hp;
    const double reference = std::tan(std::numbers::pi * 38.13547087602444 / 48000.0);
    const double gain = (1.0 + reference / hpQ + reference * reference) / hpA0;
    return {{
        {{{(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0}},
            {{1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0}}},
        {{{gain, -2.0 * gain, gain}},
            {{1.0, 2.0 * (hp * hp - 1.0) / hpA0, (1.0 - hp / hpQ + hp * hp) / hpA0}}}
    }};
}

inline double kWeightingPower(const std::array<KWeightingSection, 2>& sections, double frequency, double sampleRate) {
    const auto z = std::polar(1.0, -2.0 * std::numbers::pi * frequency / sampleRate);
    double power = 1.0;
    for (const auto& section : sections)
        power *= std::norm((section.b[0] + section.b[1] * z + section.b[2] * z * z) /
            (section.a[0] + section.a[1] * z + section.a[2] * z * z));
    return power;
}

}

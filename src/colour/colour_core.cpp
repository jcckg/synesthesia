#include "colour/colour_core.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#include "audio/analysis/phase/phase_features.h"
#include "colour/cie_2006.h"

namespace {

using ColourSpace = ColourCore::ColourSpace;
using OutputSettings = ColourCore::OutputSettings;
using FrameResult = ColourCore::FrameResult;

constexpr float kMinLoudnessDb = -70.0f;
constexpr float kMaxLoudnessDb = 0.0f;
constexpr float kRmsEpsilon = 1e-12f;
constexpr float kPerceptualReferenceLufs = -23.0f;
constexpr float kEpsilonSmall = 1e-6f;
constexpr float kEpsilonTiny = 1e-10f;
constexpr float kSrgbEncodeThreshold = 0.0031308f;
constexpr float kSrgbDecodeThreshold = 0.04045f;

// CIE D50 and D65 values are integrated from the official CIE SPDs against the
// CIE 2006 2° CMFs at 1 nm spacing.
constexpr ColourCore::WhitePoint kD50White{
    0.9656938793f,
    1.0f,
    0.8054430783f
};

constexpr ColourCore::WhitePoint kD65White{
    0.9475535595f,
    1.0f,
    1.0754043442f
};

constexpr float kD50UPrime = 0.2101387586f;
constexpr float kD50VPrime = 0.4896087848f;

enum class TransferFunction {
    SRGB,
    Rec2020
};

struct OutputProfileDefinition {
    std::array<float, 9> xyzToRgb;
    std::array<float, 9> rgbToXyz;
    TransferFunction transfer;
    ColourCore::VideoProfile videoProfile;
};

constexpr std::array<float, 9> kXYZToOklabLms{
    0.8190224379967030f, 0.3619062600528904f, -0.1288737815209879f,
    0.0329836539323885f, 0.9292868615863434f, 0.0361446663506424f,
    0.0481771893596242f, 0.2642395317527308f, 0.6335478284694309f
};

constexpr std::array<float, 9> kOklabLmsToXYZ{
    1.2268798758459243f, -0.5578149944602171f, 0.2813910456659647f,
    -0.0405757452148008f, 1.1122868032803170f, -0.0717110580655164f,
    -0.0763729366746601f, -0.4214933324022432f, 1.5869240198367816f
};

constexpr OutputProfileDefinition kRec2020Definition{
    .xyzToRgb = {1.7166512f, -0.35567078f, -0.25336629f,
                 -0.66668433f, 1.6164813f, 0.01576854f,
                 0.01763986f, -0.04277061f, 0.94210315f},
    .rgbToXyz = {0.63695806f, 0.1446169f, 0.16888098f,
                 0.2627002f, 0.67799807f, 0.05930172f,
                 0.0f, 0.02807269f, 1.0609851f},
    .transfer = TransferFunction::Rec2020,
    .videoProfile = {
        "colorspace=ispace=gbr:iprimaries=bt2020:itrc=bt2020-10:irange=pc:space=bt2020ncl:primaries=bt2020:trc=bt2020-10:range=tv:format=yuv420p10le",
        "bt2020nc",
        "bt2020",
        "bt2020-10",
        "yuv420p10le"
    }
};

constexpr ColourCore::PngProfile kRec2020PngProfile{
    false,
    0,
    true,
    9,
    14,
    0,
    1
};

constexpr OutputProfileDefinition kDisplayP3Definition{
    .xyzToRgb = {2.4934969f, -0.93138361f, -0.40271077f,
                 -0.82948899f, 1.7626641f, 0.02362468f,
                 0.03584583f, -0.07617239f, 0.9568845f},
    .rgbToXyz = {0.48657095f, 0.2656677f, 0.19821729f,
                 0.22897457f, 0.69173855f, 0.07928691f,
                 0.0f, 0.04511338f, 1.0439444f},
    .transfer = TransferFunction::SRGB,
    .videoProfile = {
        "colorspace=ispace=gbr:iprimaries=smpte432:itrc=iec61966-2-1:irange=pc:space=bt709:primaries=smpte432:trc=iec61966-2-1:range=tv:format=yuv420p",
        "bt709",
        "smpte432",
        "iec61966-2-1",
        "yuv420p"
    }
};

constexpr ColourCore::PngProfile kDisplayP3PngProfile{
    false,
    0,
    true,
    12,
    13,
    0,
    1
};

constexpr OutputProfileDefinition kSrgbDefinition{
    .xyzToRgb = {3.2409699f, -1.5373832f, -0.49861076f,
                 -0.96924365f, 1.8759675f, 0.04155506f,
                 0.05563008f, -0.20397697f, 1.0569714f},
    .rgbToXyz = {0.4123908f, 0.35758433f, 0.18048079f,
                 0.212639f, 0.7151687f, 0.07219231f,
                 0.01933082f, 0.11919478f, 0.95053214f},
    .transfer = TransferFunction::SRGB,
    .videoProfile = {
        "colorspace=ispace=gbr:iprimaries=bt709:itrc=iec61966-2-1:irange=pc:space=bt709:primaries=bt709:trc=iec61966-2-1:range=tv:format=yuv420p",
        "bt709",
        "bt709",
        "iec61966-2-1",
        "yuv420p"
    }
};

constexpr ColourCore::PngProfile kSrgbPngProfile{
    true,
    1,
    true,
    1,
    13,
    0,
    1
};

struct SpectralBandBalance {
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float tilt = 0.0f;
};

const OutputProfileDefinition& outputProfileDefinition(const ColourSpace colourSpace) {
    switch (colourSpace) {
        case ColourSpace::Rec2020:
            return kRec2020Definition;
        case ColourSpace::DisplayP3:
            return kDisplayP3Definition;
        case ColourSpace::SRGB:
        default:
            return kSrgbDefinition;
    }
}

const ColourCore::PngProfile& pngProfileDefinition(const ColourSpace colourSpace) {
    switch (colourSpace) {
        case ColourSpace::Rec2020:
            return kRec2020PngProfile;
        case ColourSpace::DisplayP3:
            return kDisplayP3PngProfile;
        case ColourSpace::SRGB:
        default:
            return kSrgbPngProfile;
    }
}

std::array<float, 3> multiplyMatrix(const std::array<float, 9>& matrix,
                                    const float x,
                                    const float y,
                                    const float z) {
    return {
        matrix[0] * x + matrix[1] * y + matrix[2] * z,
        matrix[3] * x + matrix[4] * y + matrix[5] * z,
        matrix[6] * x + matrix[7] * y + matrix[8] * z
    };
}

float encodeSrgb(const float value) {
    const float absValue = std::abs(value);
    if (absValue <= kSrgbEncodeThreshold) {
        return 12.92f * value;
    }

    const float encoded = 1.055f * std::pow(absValue, 1.0f / 2.4f) - 0.055f;
    return std::copysign(encoded, value);
}

float decodeSrgb(const float value) {
    const float absValue = std::abs(value);
    if (absValue <= kSrgbDecodeThreshold) {
        return value / 12.92f;
    }

    const float decoded = std::pow((absValue + 0.055f) / 1.055f, 2.4f);
    return std::copysign(decoded, value);
}

float encodeRec2020(const float value) {
    constexpr float alpha = 1.0992968f;
    constexpr float beta = 0.01805397f;

    const float absValue = std::abs(value);
    if (absValue < beta) {
        return 4.5f * value;
    }

    const float encoded = alpha * std::pow(absValue, 0.45f) - (alpha - 1.0f);
    return std::copysign(encoded, value);
}

float decodeRec2020(const float value) {
    constexpr float alpha = 1.0992968f;
    constexpr float beta = 0.01805397f;

    const float absValue = std::abs(value);
    if (absValue < 4.5f * beta) {
        return value / 4.5f;
    }

    const float decoded = std::pow((absValue + (alpha - 1.0f)) / alpha, 1.0f / 0.45f);
    return std::copysign(decoded, value);
}

float encodeTransferValue(const TransferFunction transfer, const float value) {
    switch (transfer) {
        case TransferFunction::Rec2020:
            return encodeRec2020(value);
        case TransferFunction::SRGB:
        default:
            return encodeSrgb(value);
    }
}

float decodeTransferValue(const TransferFunction transfer, const float value) {
    switch (transfer) {
        case TransferFunction::Rec2020:
            return decodeRec2020(value);
        case TransferFunction::SRGB:
        default:
            return decodeSrgb(value);
    }
}

void mixTowardsWhite(float& r, float& g, float& b) {
    float mixFactor = 1.0f;
    if (r < 0.0f) {
        mixFactor = std::min(mixFactor, 1.0f / (1.0f - r));
    }
    if (g < 0.0f) {
        mixFactor = std::min(mixFactor, 1.0f / (1.0f - g));
    }
    if (b < 0.0f) {
        mixFactor = std::min(mixFactor, 1.0f / (1.0f - b));
    }

    if (mixFactor < 1.0f) {
        const auto mixChannel = [mixFactor](const float channel) {
            return 1.0f + mixFactor * (channel - 1.0f);
        };

        r = mixChannel(r);
        g = mixChannel(g);
        b = mixChannel(b);
    }
}

void gamutMapRgb(float& r, float& g, float& b) {
    const bool isOutOfGamut =
        r < 0.0f || r > 1.0f ||
        g < 0.0f || g > 1.0f ||
        b < 0.0f || b > 1.0f;
    if (!isOutOfGamut) {
        return;
    }

    mixTowardsWhite(r, g, b);

    const float maxComponent = std::max({r, g, b});
    if (maxComponent > 1.0f && maxComponent > 0.0f) {
        const float scale = 1.0f / maxComponent;
        r *= scale;
        g *= scale;
        b *= scale;
    }

    r = std::clamp(r, 0.0f, 1.0f);
    g = std::clamp(g, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);
}

float soneFromLoudness(const float loudnessDb) {
    const float relative = (loudnessDb - kPerceptualReferenceLufs) / 10.0f;
    return std::pow(2.0f, relative);
}

float clampLoudnessDb(const float value) {
    if (!std::isfinite(value)) {
        return kMinLoudnessDb;
    }
    return std::clamp(value, kMinLoudnessDb, kMaxLoudnessDb);
}

float calculateLoudnessDbFromEnergy(const float totalEnergy, const size_t binCount) {
    if (binCount == 0) {
        return kMinLoudnessDb;
    }

    const float meanSquare = totalEnergy / static_cast<float>(binCount);
    if (meanSquare <= kRmsEpsilon) {
        return kMinLoudnessDb;
    }

    const float rms = std::sqrt(std::max(meanSquare, kRmsEpsilon));
    return 20.0f * std::log10(std::max(rms, kRmsEpsilon));
}

float normaliseLoudness(const float clampedDb) {
    return (clampedDb - kMinLoudnessDb) / (kMaxLoudnessDb - kMinLoudnessDb);
}

float logisticSone(const float sone) {
    return sone / (sone + 1.0f);
}

float loudnessToBrightness(const float clampedDb) {
    const float soneValue = soneFromLoudness(clampedDb);
    const float logisticValue = logisticSone(soneValue);
    const float logisticMin = logisticSone(soneFromLoudness(kMinLoudnessDb));
    const float logisticMax = logisticSone(soneFromLoudness(kMaxLoudnessDb));

    float normalised = 0.0f;
    if (logisticMax - logisticMin > kEpsilonSmall) {
        normalised = (logisticValue - logisticMin) / (logisticMax - logisticMin);
    }
    normalised = std::clamp(normalised, 0.0f, 1.0f);
    return std::pow(normalised, 1.1f);
}

float resolveBrightnessLoudnessDb(const float frameLoudnessDb,
                                  const float slowLoudnessDb,
                                  const float crestFactor,
                                  float& transientMix) {
    const float clampedFrame = clampLoudnessDb(frameLoudnessDb);
    if (!std::isfinite(slowLoudnessDb)) {
        transientMix = 1.0f;
        return clampedFrame;
    }

    const float clampedSlow = clampLoudnessDb(slowLoudnessDb);
    const float attackDb = std::max(0.0f, clampedFrame - clampedSlow);
    const float crestMix = std::clamp((crestFactor - 1.5f) / 4.5f, 0.0f, 1.0f);
    const float attackMix = std::clamp(attackDb / 12.0f, 0.0f, 1.0f);
    transientMix = attackMix * (0.35f + 0.65f * crestMix);
    return clampedSlow + transientMix * attackDb;
}

float normaliseLogFrequency(const float frequency) {
    if (!std::isfinite(frequency) || frequency <= synesthesia::constants::MIN_AUDIO_FREQ) {
        return 0.0f;
    }

    const float minLog = std::log(synesthesia::constants::MIN_AUDIO_FREQ);
    const float maxLog = std::log(synesthesia::constants::MAX_AUDIO_FREQ);
    const float freqLog = std::log(std::clamp(
        frequency,
        synesthesia::constants::MIN_AUDIO_FREQ,
        synesthesia::constants::MAX_AUDIO_FREQ));

    return std::clamp((freqLog - minLog) / std::max(maxLog - minLog, kEpsilonSmall), 0.0f, 1.0f);
}

float blendLogFrequencies(const float lhs, const float rhs, const float blend) {
    const float clampedLhs = std::clamp(
        std::isfinite(lhs) ? lhs : synesthesia::constants::MIN_AUDIO_FREQ,
        synesthesia::constants::MIN_AUDIO_FREQ,
        synesthesia::constants::MAX_AUDIO_FREQ);
    const float clampedRhs = std::clamp(
        std::isfinite(rhs) ? rhs : clampedLhs,
        synesthesia::constants::MIN_AUDIO_FREQ,
        synesthesia::constants::MAX_AUDIO_FREQ);

    return std::exp(std::lerp(
        std::log(clampedLhs),
        std::log(clampedRhs),
        std::clamp(blend, 0.0f, 1.0f)));
}

SpectralBandBalance calculateBandBalance(std::span<const float> magnitudes,
                                         std::span<const float> frequencies) {
    SpectralBandBalance balance{};
    if (magnitudes.size() != frequencies.size()) {
        return balance;
    }

    for (size_t i = 0; i < magnitudes.size(); ++i) {
        const float magnitude = magnitudes[i];
        const float frequency = frequencies[i];
        if (!std::isfinite(magnitude) || !std::isfinite(frequency) || magnitude <= 0.0f) {
            continue;
        }

        const float energy = magnitude * magnitude;
        if (frequency < 220.0f) {
            balance.low += energy;
        } else if (frequency < 2200.0f) {
            balance.mid += energy;
        } else {
            balance.high += energy;
        }
    }

    const float total = balance.low + balance.mid + balance.high;
    if (total <= kRmsEpsilon) {
        return balance;
    }

    const float weightedLow = balance.low + 0.5f * balance.mid;
    const float weightedHigh = balance.high + 0.5f * balance.mid;
    balance.tilt = std::clamp((weightedHigh - weightedLow) / total, -1.0f, 1.0f);
    return balance;
}

float calculateSpectralCentroid(std::span<const float> magnitudes,
                                std::span<const float> frequencies) {
    float weightedSum = 0.0f;
    float totalWeight = 0.0f;

    for (size_t i = 0; i < magnitudes.size() && i < frequencies.size(); ++i) {
        const float magnitude = magnitudes[i];
        const float frequency = frequencies[i];
        if (frequency < synesthesia::constants::MIN_AUDIO_FREQ ||
            frequency > synesthesia::constants::MAX_AUDIO_FREQ ||
            magnitude <= kEpsilonSmall) {
            continue;
        }

        weightedSum += frequency * magnitude;
        totalWeight += magnitude;
    }

    return totalWeight > kEpsilonSmall ? weightedSum / totalWeight : 0.0f;
}

float calculateSpectralSpread(std::span<const float> magnitudes,
                              std::span<const float> frequencies,
                              const float centroid) {
    float spreadSum = 0.0f;
    float totalWeight = 0.0f;

    for (size_t i = 0; i < magnitudes.size() && i < frequencies.size(); ++i) {
        const float magnitude = magnitudes[i];
        const float frequency = frequencies[i];
        if (frequency < synesthesia::constants::MIN_AUDIO_FREQ ||
            frequency > synesthesia::constants::MAX_AUDIO_FREQ ||
            magnitude <= kEpsilonSmall) {
            continue;
        }

        const float diff = frequency - centroid;
        spreadSum += magnitude * diff * diff;
        totalWeight += magnitude;
    }

    return totalWeight > kEpsilonSmall ? std::sqrt(spreadSum / totalWeight) : 0.0f;
}

float calculateSpectralFlatness(std::span<const float> magnitudes) {
    float geometricSum = 0.0f;
    float arithmeticSum = 0.0f;
    int count = 0;

    for (const float magnitude : magnitudes) {
        if (magnitude > kEpsilonSmall) {
            geometricSum += std::log(magnitude);
            arithmeticSum += magnitude;
            ++count;
        }
    }

    if (count == 0 || arithmeticSum < kEpsilonSmall) {
        return 0.5f;
    }

    const float geometricMean = std::exp(geometricSum / static_cast<float>(count));
    const float arithmeticMean = arithmeticSum / static_cast<float>(count);
    return geometricMean / arithmeticMean;
}

float calculateSpectralRolloff(std::span<const float> magnitudes,
                               std::span<const float> frequencies,
                               const float threshold = 0.85f) {
    if (magnitudes.size() != frequencies.size() || magnitudes.empty()) {
        return 0.0f;
    }

    float totalEnergy = 0.0f;
    for (const float magnitude : magnitudes) {
        totalEnergy += magnitude * magnitude;
    }
    if (totalEnergy < kEpsilonSmall) {
        return 0.0f;
    }

    const float targetEnergy = totalEnergy * threshold;
    float cumulativeEnergy = 0.0f;

    for (size_t i = 0; i < magnitudes.size(); ++i) {
        cumulativeEnergy += magnitudes[i] * magnitudes[i];
        if (cumulativeEnergy >= targetEnergy) {
            return frequencies[i];
        }
    }

    return frequencies.back();
}

float calculateSpectralCrestFactor(std::span<const float> magnitudes,
                                   const float maxMagnitude,
                                   const float totalEnergy) {
    if (magnitudes.empty() ||
        !std::isfinite(maxMagnitude) ||
        !std::isfinite(totalEnergy) ||
        maxMagnitude < kEpsilonSmall ||
        totalEnergy < kEpsilonSmall) {
        return 1.0f;
    }

    const float rms = std::sqrt(totalEnergy / static_cast<float>(magnitudes.size()));
    if (!std::isfinite(rms) || rms < kEpsilonSmall) {
        return 1.0f;
    }

    return maxMagnitude / rms;
}

ColourCore::XYZ integrateSpectrum2006(std::span<const float> magnitudes,
                                      std::span<const float> frequencies) {
    ColourCore::XYZ total{};

    for (size_t i = 0; i < magnitudes.size() && i < frequencies.size(); ++i) {
        const float magnitude = magnitudes[i];
        const float frequency = frequencies[i];
        if (frequency < synesthesia::constants::MIN_AUDIO_FREQ ||
            frequency > synesthesia::constants::MAX_AUDIO_FREQ ||
            magnitude <= kEpsilonSmall) {
            continue;
        }

        const float wavelength = ColourCore::logFrequencyToWavelength(frequency);
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        ColourCore::interpolateCIE(wavelength, X, Y, Z);

        total.X += magnitude * X;
        total.Y += magnitude * Y;
        total.Z += magnitude * Z;
    }

    return total;
}

float helmholtzKohlrauschCorrection(const float X,
                                    const float Y,
                                    const float Z,
                                    const float strength = 0.35f) {
    const float sum = X + 15.0f * Y + 3.0f * Z;
    if (sum <= kEpsilonTiny) {
        return 1.0f;
    }

    const float uPrime = 4.0f * X / sum;
    const float vPrime = 9.0f * Y / sum;
    const float du = uPrime - kD50UPrime;
    const float dv = vPrime - kD50VPrime;

    const float suv = 13.0f * std::sqrt(du * du + dv * dv);
    if (suv < kEpsilonSmall) {
        return 1.0f;
    }

    const float theta = std::atan2(dv, du);
    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);
    const float cos2T = 2.0f * cosT * cosT - 1.0f;
    const float sin2T = 2.0f * sinT * cosT;
    const float cos3T = cosT * cos2T - sinT * sin2T;
    const float sin3T = sinT * cos2T + cosT * sin2T;
    const float cos4T = cos2T * cos2T - sin2T * sin2T;
    const float sin4T = 2.0f * sin2T * cos2T;

    const float q =
        -0.01585f
        - 0.03017f * cosT - 0.04556f * cos2T - 0.02667f * cos3T - 0.00295f * cos4T
        + 0.14592f * sinT + 0.05084f * sin2T - 0.01900f * sin3T - 0.00764f * sin4T;

    constexpr float laPow = 7.9250f;
    constexpr float kBr = 0.2717f * (6.469f + 6.362f * laPow) / (6.469f + laPow);

    const float inner = 1.0f + (-0.8660f * q + 0.0872f * kBr) * suv + 0.3086f;
    const float vcc = 0.4462f * inner * inner * inner;

    const float clampedVcc = std::clamp(vcc, 0.5f, 3.0f);
    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);
    return 1.0f + clampedStrength * (clampedVcc - 1.0f);
}

void applyPhaseInfluence(FrameResult& result,
                         const PhaseAnalysis::PhaseFeatureMetrics* phaseMetrics,
                         const OutputSettings& outputSettings) {
    if (phaseMetrics == nullptr) {
        return;
    }

    result.phaseInstabilityNorm = phaseMetrics->instabilityNorm;
    result.phaseCoherenceNorm = phaseMetrics->coherenceNorm;
    result.phaseTransientNorm = phaseMetrics->transientNorm;

    if (phaseMetrics->isNeutral()) {
        return;
    }

    float oklabL = 0.0f;
    float oklabA = 0.0f;
    float oklabB = 0.0f;
    ColourCore::XYZtoOklab(result.X, result.Y, result.Z, oklabL, oklabA, oklabB);

    const float coherence = std::clamp(phaseMetrics->coherenceNorm, 0.0f, 1.0f);
    const float instability = std::clamp(phaseMetrics->instabilityNorm, 0.0f, 1.0f);
    const float transient = std::clamp(phaseMetrics->transientNorm, 0.0f, 1.0f);
    const float stableWarmth = coherence * (1.0f - instability);
    const float unstableCoolness = instability * (1.0f - 0.3f * coherence);
    const float chromaScale = std::clamp(
        1.0f + 0.06f * stableWarmth - 0.05f * unstableCoolness,
        0.92f,
        1.08f);

    oklabA = oklabA * chromaScale + stableWarmth * 1.0f - unstableCoolness * 0.6f;
    oklabB = oklabB * chromaScale + stableWarmth * 0.6f - unstableCoolness * 1.4f;
    oklabL = std::clamp(
        oklabL + transient * (1.5f + 2.5f * std::clamp(result.brightnessNormalised, 0.0f, 1.0f)) - unstableCoolness * 0.6f,
        0.0f,
        100.0f);

    ColourCore::OklabtoXYZ(oklabL, oklabA, oklabB, result.X, result.Y, result.Z);
    ColourCore::XYZtoLab(result.X, result.Y, result.Z, result.L, result.a, result.b_comp);
    ColourCore::XYZtoRGB(result.X, result.Y, result.Z,
                         result.r, result.g, result.b,
                         outputSettings.colourSpace,
                         true,
                         outputSettings.applyGamutMapping);
    result.luminanceCdM2 =
        std::max(0.0f, result.Y) * synesthesia::constants::REFERENCE_WHITE_LUMINANCE_CDM2;
}

} // namespace

namespace ColourCore {

WhitePoint D50White() {
    return kD50White;
}

WhitePoint D65White() {
    return kD65White;
}

void interpolateCIE(const float wavelength, float& X, float& Y, float& Z) {
    Colour::CIE2006::interpolate(wavelength, X, Y, Z);
}

float logFrequencyToWavelength(const float frequency) {
    if (!std::isfinite(frequency) || frequency <= 0.0f) {
        return synesthesia::constants::MAX_WAVELENGTH_NM;
    }

    constexpr float minWavelength = 390.0f;
    constexpr float maxWavelength = 700.0f;

    if (frequency < synesthesia::constants::MIN_AUDIO_FREQ) {
        const float subMin = 0.1f;
        const float tSub = std::clamp(
            (frequency - subMin) / (synesthesia::constants::MIN_AUDIO_FREQ - subMin),
            0.0f,
            1.0f);
        return 900.0f - tSub * (900.0f - maxWavelength);
    }

    const float minLog = std::log2(synesthesia::constants::MIN_AUDIO_FREQ);
    const float maxLog = std::log2(synesthesia::constants::MAX_AUDIO_FREQ);
    const float logFrequency = std::log2(frequency);
    const float normalised = (logFrequency - minLog) / (maxLog - minLog);
    const float t = std::clamp(normalised, 0.0f, 1.0f);
    return maxWavelength - t * (maxWavelength - minWavelength);
}

float wavelengthToLogFrequency(const float wavelengthIn) {
    constexpr float minWavelength = 390.0f;
    constexpr float maxWavelength = 700.0f;

    const float wavelength = std::clamp(
        std::isfinite(wavelengthIn) ? wavelengthIn : maxWavelength,
        minWavelength,
        maxWavelength);
    const float t = (maxWavelength - wavelength) / (maxWavelength - minWavelength);
    const float minLog = std::log2(synesthesia::constants::MIN_AUDIO_FREQ);
    const float maxLog = std::log2(synesthesia::constants::MAX_AUDIO_FREQ);
    return std::pow(2.0f, minLog + t * (maxLog - minLog));
}

float chromaticDominantWavelength(const float X, const float Y, const float Z) {
    const float sum = X + Y + Z;
    if (!std::isfinite(sum) || sum <= 0.0f) {
        return synesthesia::constants::MAX_WAVELENGTH_NM;
    }

    const float x = X / sum;
    const float y = Y / sum;

    const float whiteSum = kD50White.X + kD50White.Y + kD50White.Z;
    const float whiteX = kD50White.X / whiteSum;
    const float whiteY = kD50White.Y / whiteSum;

    const float rayDx = x - whiteX;
    const float rayDy = y - whiteY;
    const float rayLengthSquared = rayDx * rayDx + rayDy * rayDy;
    if (rayLengthSquared < kEpsilonTiny) {
        return synesthesia::constants::MAX_WAVELENGTH_NM;
    }

    bool found = false;
    float bestT = std::numeric_limits<float>::max();
    float bestWavelength = synesthesia::constants::MAX_WAVELENGTH_NM;

    auto chromaticityAt = [](const float wavelength) {
        float sampleX = 0.0f;
        float sampleY = 0.0f;
        float sampleZ = 0.0f;
        ColourCore::interpolateCIE(wavelength, sampleX, sampleY, sampleZ);
        const float sampleSum = sampleX + sampleY + sampleZ;
        if (sampleSum <= 0.0f) {
            return std::array<float, 2>{0.0f, 0.0f};
        }
        return std::array<float, 2>{sampleX / sampleSum, sampleY / sampleSum};
    };

    for (int wavelength = 390; wavelength < 830; ++wavelength) {
        const auto c0 = chromaticityAt(static_cast<float>(wavelength));
        const auto c1 = chromaticityAt(static_cast<float>(wavelength + 1));

        const float segDx = c1[0] - c0[0];
        const float segDy = c1[1] - c0[1];
        const float det = rayDx * (-segDy) - rayDy * (-segDx);
        if (std::abs(det) < kEpsilonTiny) {
            continue;
        }

        const float px = c0[0] - whiteX;
        const float py = c0[1] - whiteY;
        const float t = (px * (-segDy) - py * (-segDx)) / det;
        const float u = (rayDx * py - rayDy * px) / det;

        if (t >= 0.0f && u >= 0.0f && u <= 1.0f && t < bestT) {
            found = true;
            bestT = t;
            bestWavelength = std::clamp(
                std::lerp(static_cast<float>(wavelength), static_cast<float>(wavelength + 1), u),
                synesthesia::constants::MIN_WAVELENGTH_NM,
                synesthesia::constants::MAX_WAVELENGTH_NM);
        }
    }

    if (found) {
        return bestWavelength;
    }

    const float rayLength = std::sqrt(rayLengthSquared);
    float bestCosine = -1.0f;
    float bestDistance = std::numeric_limits<float>::max();

    for (int wavelength = 390; wavelength <= 830; ++wavelength) {
        const auto chromaticity = chromaticityAt(static_cast<float>(wavelength));
        const float vecX = chromaticity[0] - whiteX;
        const float vecY = chromaticity[1] - whiteY;
        const float vectorLength = std::sqrt(vecX * vecX + vecY * vecY);
        if (vectorLength < kEpsilonSmall) {
            continue;
        }

        const float dot = rayDx * vecX + rayDy * vecY;
        const float cosine = dot / (rayLength * vectorLength);
        if (!std::isfinite(cosine) || cosine < 0.0f) {
            continue;
        }

        const float cross = rayDx * vecY - rayDy * vecX;
        const float distance = std::abs(cross) / vectorLength;
        if (cosine > bestCosine ||
            (std::abs(cosine - bestCosine) < 1e-4f && distance < bestDistance)) {
            bestCosine = cosine;
            bestDistance = distance;
            bestWavelength = static_cast<float>(wavelength);
        }
    }

    return bestWavelength;
}

void XYZtoLab(const float X, const float Y, const float Z, float& L, float& a, float& bValue) {
    const float xr = kD50White.X > 0.0f ? X / kD50White.X : 0.0f;
    const float yr = kD50White.Y > 0.0f ? Y / kD50White.Y : 0.0f;
    const float zr = kD50White.Z > 0.0f ? Z / kD50White.Z : 0.0f;

    auto f = [](const float value) {
        constexpr float epsilon = synesthesia::constants::LAB_EPSILON;
        constexpr float kappa = synesthesia::constants::LAB_KAPPA;
        return value > epsilon ? std::pow(value, 1.0f / 3.0f) : (kappa * value + 16.0f) / 116.0f;
    };

    const float fx = f(xr);
    const float fy = f(yr);
    const float fz = f(zr);

    L = 116.0f * fy - 16.0f;
    a = 500.0f * (fx - fy);
    bValue = 200.0f * (fy - fz);
}

void LabtoXYZ(const float L, const float a, const float bValue, float& X, float& Y, float& Z) {
    const float fY = (L + 16.0f) / 116.0f;
    const float fX = fY + a / 500.0f;
    const float fZ = fY - bValue / 200.0f;

    auto inverse = [](const float value) {
        constexpr float delta = synesthesia::constants::LAB_DELTA;
        constexpr float deltaSquared = delta * delta;
        return value > delta ? std::pow(value, 3.0f) : 3.0f * deltaSquared * (value - 4.0f / 29.0f);
    };

    X = kD50White.X * inverse(fX);
    Y = kD50White.Y * inverse(fY);
    Z = kD50White.Z * inverse(fZ);
}

void XYZtoOklab(const float X, const float Y, const float Z, float& L, float& a, float& bValue) {
    const auto lms = multiplyMatrix(kXYZToOklabLms, X, Y, Z);
    const float l = std::cbrt(lms[0]);
    const float m = std::cbrt(lms[1]);
    const float s = std::cbrt(lms[2]);

    L = (0.2104542683093140f * l + 0.7936177747023054f * m - 0.0040720430116193f * s) * 100.0f;
    a = (1.9779985324311684f * l - 2.4285922420485799f * m + 0.4505937096174110f * s) * 100.0f;
    bValue = (0.0259040424655478f * l + 0.7827717124575296f * m - 0.8086757549230774f * s) * 100.0f;
}

void OklabtoXYZ(const float L, const float a, const float bValue, float& X, float& Y, float& Z) {
    const float l = L / 100.0f + 0.3963377773761749f * (a / 100.0f) + 0.2158037573099136f * (bValue / 100.0f);
    const float m = L / 100.0f - 0.1055613458156586f * (a / 100.0f) - 0.0638541728258133f * (bValue / 100.0f);
    const float s = L / 100.0f - 0.0894841775298119f * (a / 100.0f) - 1.2914855480194092f * (bValue / 100.0f);
    const auto xyz = multiplyMatrix(kOklabLmsToXYZ, l * l * l, m * m * m, s * s * s);
    X = xyz[0];
    Y = xyz[1];
    Z = xyz[2];
}

void RGBtoXYZ(const float r, const float g, const float b, float& X, float& Y, float& Z,
              const ColourSpace colourSpace) {
    const auto& definition = outputProfileDefinition(colourSpace);
    const float linearR = decodeTransferValue(definition.transfer, r);
    const float linearG = decodeTransferValue(definition.transfer, g);
    const float linearB = decodeTransferValue(definition.transfer, b);
    const auto xyz = multiplyMatrix(definition.rgbToXyz, linearR, linearG, linearB);
    X = xyz[0];
    Y = xyz[1];
    Z = xyz[2];
}

void XYZtoRGB(const float X, const float Y, const float Z,
              float& r, float& g, float& b,
              const ColourSpace colourSpace,
              const bool applyGamma,
              const bool applyGamutMapping) {
    const auto& definition = outputProfileDefinition(colourSpace);
    const auto linearRgb = multiplyMatrix(definition.xyzToRgb, X, Y, Z);

    float linearR = linearRgb[0];
    float linearG = linearRgb[1];
    float linearB = linearRgb[2];

    if (applyGamutMapping) {
        mixTowardsWhite(linearR, linearG, linearB);
        gamutMapRgb(linearR, linearG, linearB);
        linearR = std::clamp(linearR, 0.0f, 1.0f);
        linearG = std::clamp(linearG, 0.0f, 1.0f);
        linearB = std::clamp(linearB, 0.0f, 1.0f);
    }

    if (!applyGamma) {
        r = linearR;
        g = linearG;
        b = linearB;
        return;
    }

    r = encodeTransferValue(definition.transfer, linearR);
    g = encodeTransferValue(definition.transfer, linearG);
    b = encodeTransferValue(definition.transfer, linearB);

    if (applyGamutMapping) {
        r = std::clamp(r, 0.0f, 1.0f);
        g = std::clamp(g, 0.0f, 1.0f);
        b = std::clamp(b, 0.0f, 1.0f);
    }
}

void RGBtoLab(const float r, const float g, const float b, float& L, float& a, float& bValue,
              const ColourSpace colourSpace) {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    ColourCore::RGBtoXYZ(r, g, b, X, Y, Z, colourSpace);
    ColourCore::XYZtoLab(X, Y, Z, L, a, bValue);
}

void LabtoRGB(const float L, const float a, const float bValue,
              float& r, float& g, float& bOut,
              const ColourSpace colourSpace,
              const bool applyGamutMapping) {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    ColourCore::LabtoXYZ(L, a, bValue, X, Y, Z);
    ColourCore::XYZtoRGB(X, Y, Z, r, g, bOut, colourSpace, true, applyGamutMapping);
}

RGB projectToRGB(const XYZ& xyz, const OutputSettings& settings) {
    RGB rgb{};
    ColourCore::XYZtoRGB(
        xyz.X,
        xyz.Y,
        xyz.Z,
        rgb.r,
        rgb.g,
        rgb.b,
        settings.colourSpace,
        true,
        settings.applyGamutMapping);
    return rgb;
}

const VideoProfile& videoProfileFor(const ColourSpace colourSpace) {
    return outputProfileDefinition(colourSpace).videoProfile;
}

const PngProfile& pngProfileFor(const ColourSpace colourSpace) {
    return pngProfileDefinition(colourSpace);
}

SpectralCharacteristics calculateSpectralCharacteristics(std::span<const float> spectrum, const float sampleRate) {
    SpectralCharacteristics result{};
    if (spectrum.empty() || sampleRate <= 0.0f) {
        return result;
    }

    float totalWeight = 0.0f;
    float weightedFrequencySum = 0.0f;
    float logSum = 0.0f;
    int count = 0;

    for (size_t i = 0; i < spectrum.size(); ++i) {
        const float value = spectrum[i];
        if (value <= kEpsilonSmall || !std::isfinite(value)) {
            continue;
        }

        const float frequency = spectrum.size() <= 1
            ? 0.0f
            : static_cast<float>(i) * sampleRate / (2.0f * static_cast<float>(spectrum.size() - 1));
        if (frequency < synesthesia::constants::MIN_AUDIO_FREQ ||
            frequency > synesthesia::constants::MAX_AUDIO_FREQ) {
            continue;
        }

        totalWeight += value;
        weightedFrequencySum += frequency * value;
        logSum += std::log(value);
        ++count;
    }

    if (count == 0 || totalWeight <= 0.0f) {
        return result;
    }

    const float arithmeticMean = totalWeight / static_cast<float>(count);
    if (arithmeticMean > kEpsilonTiny) {
        result.flatness = std::exp(logSum / static_cast<float>(count)) / arithmeticMean;
    }

    result.centroid = weightedFrequencySum / totalWeight;

    float spreadSum = 0.0f;
    for (size_t i = 0; i < spectrum.size(); ++i) {
        const float value = spectrum[i];
        if (value <= kEpsilonSmall || !std::isfinite(value)) {
            continue;
        }

        const float frequency = spectrum.size() <= 1
            ? 0.0f
            : static_cast<float>(i) * sampleRate / (2.0f * static_cast<float>(spectrum.size() - 1));
        if (frequency < synesthesia::constants::MIN_AUDIO_FREQ ||
            frequency > synesthesia::constants::MAX_AUDIO_FREQ) {
            continue;
        }

        const float diff = frequency - result.centroid;
        spreadSum += value * diff * diff;
    }

    result.spread = std::sqrt(spreadSum / totalWeight);
    result.normalisedSpread = std::min(result.spread / 5000.0f, 1.0f);
    return result;
}

FrameResult analyseSpectrum(std::span<const float> magnitudes,
                            std::span<const float> phases,
                            std::span<const float> frequencies,
                            const float sampleRate,
                            const OutputSettings& outputSettings,
                            const float overrideLoudnessDb,
                            const PhaseAnalysis::PhaseFeatureMetrics* phaseMetrics) {
    FrameResult result{};

    if (magnitudes.empty() || sampleRate <= 0.0f) {
        result.r = 0.1f;
        result.g = 0.1f;
        result.b = 0.1f;
        result.loudnessDb = kMinLoudnessDb;
        result.frameLoudnessDb = kMinLoudnessDb;
        result.brightnessLoudnessDb = kMinLoudnessDb;
        result.estimatedSPL = synesthesia::constants::REFERENCE_SPL_AT_0_LUFS + kMinLoudnessDb;
        return result;
    }

    std::vector<float> cleanMagnitudes(magnitudes.size(), 0.0f);
    for (size_t i = 0; i < magnitudes.size(); ++i) {
        const float magnitude = magnitudes[i];
        cleanMagnitudes[i] = (std::isfinite(magnitude) && magnitude > 0.0f) ? magnitude : 0.0f;
    }

    std::vector<float> effectiveFrequencies;
    effectiveFrequencies.resize(cleanMagnitudes.size(), 0.0f);
    if (!frequencies.empty() && frequencies.size() == cleanMagnitudes.size()) {
        std::copy(frequencies.begin(), frequencies.end(), effectiveFrequencies.begin());
    } else if (cleanMagnitudes.size() > 1) {
        const float binSize = sampleRate / (2.0f * static_cast<float>(cleanMagnitudes.size() - 1));
        for (size_t i = 0; i < cleanMagnitudes.size(); ++i) {
            effectiveFrequencies[i] = static_cast<float>(i) * binSize;
        }
    }

    result.spectralCentroid = calculateSpectralCentroid(cleanMagnitudes, effectiveFrequencies);
    result.spectralSpread = calculateSpectralSpread(cleanMagnitudes, effectiveFrequencies, result.spectralCentroid);
    result.spectralFlatness = calculateSpectralFlatness(cleanMagnitudes);
    result.spectralRolloff = calculateSpectralRolloff(cleanMagnitudes, effectiveFrequencies);

    float maxMagnitude = 0.0f;
    float totalEnergy = 0.0f;
    for (const float magnitude : cleanMagnitudes) {
        maxMagnitude = std::max(maxMagnitude, magnitude);
        totalEnergy += magnitude * magnitude;
    }
    result.spectralCrestFactor = calculateSpectralCrestFactor(cleanMagnitudes, maxMagnitude, totalEnergy);

    const float computedLoudnessDb = calculateLoudnessDbFromEnergy(totalEnergy, cleanMagnitudes.size());
    const float loudnessDb = std::isfinite(overrideLoudnessDb) ? overrideLoudnessDb : computedLoudnessDb;
    const float clampedLoudnessDb = clampLoudnessDb(loudnessDb);
    const float loudnessNormalised = std::clamp(normaliseLoudness(clampedLoudnessDb), 0.0f, 1.0f);
    float transientMix = 0.0f;
    const float brightnessLoudnessDb = resolveBrightnessLoudnessDb(
        computedLoudnessDb,
        loudnessDb,
        result.spectralCrestFactor,
        transientMix);

    const float centroidNormalised = normaliseLogFrequency(result.spectralCentroid);
    const float rolloffNormalised = normaliseLogFrequency(result.spectralRolloff);
    const float spreadNormalised = std::clamp(
        result.spectralCentroid > 1e-3f ? (result.spectralSpread / result.spectralCentroid) / 1.75f : 0.0f,
        0.0f,
        1.0f);
    const float crestNormalised = std::clamp(
        std::log2(std::max(result.spectralCrestFactor, 1.0f)) / 3.5f,
        0.0f,
        1.0f);
    const float tonalStrength = std::clamp(
        0.55f * (1.0f - result.spectralFlatness) + 0.45f * crestNormalised,
        0.0f,
        1.0f);
    const SpectralBandBalance bandBalance = calculateBandBalance(cleanMagnitudes, effectiveFrequencies);
    const float transientAccent = transientMix * (0.35f + 0.65f * tonalStrength);
    const float brightnessGain = std::clamp(
        loudnessToBrightness(brightnessLoudnessDb) * (1.0f + 0.18f * transientAccent),
        0.0f,
        1.2f);

    const XYZ integratedXYZ = integrateSpectrum2006(cleanMagnitudes, effectiveFrequencies);

    float chromaX = 0.0f;
    float chromaY = 0.0f;
    float chromaZ = 0.0f;
    const float totalWeight = std::accumulate(cleanMagnitudes.begin(), cleanMagnitudes.end(), 0.0f);
    if (totalWeight > kEpsilonSmall) {
        const float invWeight = 1.0f / totalWeight;
        chromaX = integratedXYZ.X * invWeight;
        chromaY = integratedXYZ.Y * invWeight;
        chromaZ = integratedXYZ.Z * invWeight;
    }

    {
        const float flatness = std::clamp(result.spectralFlatness, 0.0f, 1.0f);
        const float spreadRatio =
            result.spectralCentroid > 100.0f ? result.spectralSpread / result.spectralCentroid : 0.0f;
        const float normalisedSpread = std::clamp(spreadRatio, 0.0f, 2.0f) * 0.5f;
        const float desaturation = flatness * normalisedSpread * 0.35f;
        const float saturation = std::clamp(1.0f - desaturation, 0.4f, 1.0f);

        const float neutralX = chromaY * (kD50White.X / kD50White.Y);
        const float neutralZ = chromaY * (kD50White.Z / kD50White.Y);
        chromaX = std::lerp(neutralX, chromaX, saturation);
        chromaZ = std::lerp(neutralZ, chromaZ, saturation);
    }

    const float scaledX = chromaX * brightnessGain;
    const float scaledY = chromaY * brightnessGain;
    const float scaledZ = chromaZ * brightnessGain;

    const float hkFactor = helmholtzKohlrauschCorrection(scaledX, scaledY, scaledZ);
    const float hkX = scaledX / hkFactor;
    const float hkY = scaledY / hkFactor;
    const float hkZ = scaledZ / hkFactor;

    result.loudnessDb = clampedLoudnessDb;
    result.frameLoudnessDb = clampLoudnessDb(computedLoudnessDb);
    result.brightnessLoudnessDb = brightnessLoudnessDb;
    result.loudnessNormalised = loudnessNormalised;
    result.brightnessNormalised = brightnessGain;
    result.transientMix = transientMix;
    result.estimatedSPL = synesthesia::constants::REFERENCE_SPL_AT_0_LUFS + clampedLoudnessDb;

    float baseL = 0.0f;
    float baseA = 0.0f;
    float baseB = 0.0f;
    ColourCore::XYZtoLab(hkX, hkY, hkZ, baseL, baseA, baseB);

    const float baseRadius = std::hypot(baseA, baseB);
    const float guidedFrequency = blendLogFrequencies(
        result.spectralCentroid > 1e-3f ? result.spectralCentroid : result.spectralRolloff,
        result.spectralRolloff > 1e-3f ? result.spectralRolloff : result.spectralCentroid,
        0.35f);
    const float guidedWavelength = ColourCore::logFrequencyToWavelength(guidedFrequency);

    float guidedX = 0.0f;
    float guidedY = 0.0f;
    float guidedZ = 0.0f;
    ColourCore::interpolateCIE(guidedWavelength, guidedX, guidedY, guidedZ);
    const float guidedScale = guidedY > kEpsilonSmall ? 0.35f / guidedY : 1.0f;

    float guidedL = 0.0f;
    float guidedA = 0.0f;
    float guidedB = 0.0f;
    ColourCore::XYZtoLab(
        guidedX * guidedScale,
        guidedY * guidedScale,
        guidedZ * guidedScale,
        guidedL,
        guidedA,
        guidedB);

    const float baseNormaliser = baseRadius > kEpsilonSmall ? 1.0f / baseRadius : 0.0f;
    const float guidedRadius = std::hypot(guidedA, guidedB);
    const float guidedNormaliser = guidedRadius > kEpsilonSmall ? 1.0f / guidedRadius : 0.0f;

    float hueA = baseRadius > kEpsilonSmall ? baseA * baseNormaliser : guidedA * guidedNormaliser;
    float hueB = baseRadius > kEpsilonSmall ? baseB * baseNormaliser : guidedB * guidedNormaliser;
    const float hueGuidance = std::clamp(0.25f + 0.25f * tonalStrength + 0.15f * transientAccent, 0.0f, 0.65f);
    if (guidedRadius > kEpsilonSmall) {
        hueA = std::lerp(hueA, guidedA * guidedNormaliser, hueGuidance);
        hueB = std::lerp(hueB, guidedB * guidedNormaliser, hueGuidance);
    }

    hueA += -bandBalance.tilt * 0.18f;
    hueB += -bandBalance.tilt * 0.26f;

    const float hueLength = std::hypot(hueA, hueB);
    if (hueLength > kEpsilonSmall) {
        hueA /= hueLength;
        hueB /= hueLength;
    }

    const float semanticRadiusFloor = brightnessGain * (6.0f + 18.0f * tonalStrength);
    const float radiusSeed = std::max(baseRadius, semanticRadiusFloor);
    const float radiusScale = std::clamp(
        0.72f + 0.58f * tonalStrength + 0.18f * transientAccent - 0.18f * spreadNormalised,
        0.45f,
        1.45f);
    const float finalRadius = std::clamp(radiusSeed * radiusScale, 0.0f, 96.0f);
    const float finalL = std::clamp(
        baseL + transientAccent * (4.0f + 6.0f * brightnessGain)
            + (centroidNormalised - 0.5f) * 2.0f
            + (rolloffNormalised - 0.5f) * 2.5f,
        0.0f,
        100.0f);
    const float finalA = hueA * finalRadius;
    const float finalB = hueB * finalRadius;

    ColourCore::LabtoXYZ(finalL, finalA, finalB, result.X, result.Y, result.Z);
    ColourCore::XYZtoRGB(result.X, result.Y, result.Z,
                         result.r, result.g, result.b,
                         outputSettings.colourSpace,
                         true,
                         outputSettings.applyGamutMapping);

    result.L = finalL;
    result.a = finalA;
    result.b_comp = finalB;
    result.colourIntensity = result.Y;
    result.luminanceCdM2 =
        std::max(0.0f, result.Y) * synesthesia::constants::REFERENCE_WHITE_LUMINANCE_CDM2;
    result.dominantFrequency = result.spectralCentroid;
    result.dominantWavelength = ColourCore::chromaticDominantWavelength(result.X, result.Y, result.Z);

    applyPhaseInfluence(result, phaseMetrics, outputSettings);
    result.dominantWavelength = ColourCore::chromaticDominantWavelength(result.X, result.Y, result.Z);

    (void)phases;
    return result;
}

} // namespace ColourCore

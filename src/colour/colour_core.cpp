#include "colour/colour_core.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "audio/analysis/phase/phase_features.h"
#include "colour/cie_2015.h"
#include "audio/analysis/loudness/k_weighting.h"

namespace {

using ColourSpace = ColourCore::ColourSpace;
using OutputSettings = ColourCore::OutputSettings;
using FrameResult = ColourCore::FrameResult;

constexpr float kMinLoudnessDb = -70.0f;
constexpr float kMaxLoudnessDb = 0.0f;
constexpr float kPerceptualReferenceLufs = -23.0f;
constexpr float kEpsilonSmall = 1e-6f;
constexpr float kEpsilonTiny = 1e-10f;
constexpr float kSrgbEncodeThreshold = 0.0031308f;
constexpr float kSrgbDecodeThreshold = 0.04045f;

// https://cie.co.at/datatable/cie-standard-illuminant-d50
constexpr ColourCore::WhitePoint kD50White{
    0.965693878369f,
    1.000000000000f,
    0.805443077767f
};

// https://cie.co.at/datatable/cie-standard-illuminant-d65
constexpr ColourCore::WhitePoint kD65White{
    0.947553558570f,
    1.000000000000f,
    1.075404343361f
};

enum class TransferFunction {
    SRGB,
    Rec2020
};

struct OutputProfileDefinition {
    std::array<float, 9> encodingXYZToRgb;
    std::array<float, 9> rgbToEncodingXYZ;
    TransferFunction transfer;
    ColourCore::VideoProfile videoProfile;
};

constexpr std::array<float, 9> kEncodingXYZToOklabLms{
    0.8190224379967030f, 0.3619062600528904f, -0.1288737815209879f,
    0.0329836539323885f, 0.9292868615863434f, 0.0361446663506424f,
    0.0481771893596242f, 0.2642395317527308f, 0.6335478284694309f
};

constexpr std::array<float, 9> kOklabLmsToEncodingXYZ{
    1.2268798758459243f, -0.5578149944602171f, 0.2813910456659647f,
    -0.0405757452148008f, 1.1122868032803170f, -0.0717110580655164f,
    -0.0763729366746601f, -0.4214933324022432f, 1.5869240198367816f
};

// https://ffmpeg.org/ffmpeg-filters.html#scale-1
constexpr OutputProfileDefinition kRec2020Definition{
    .encodingXYZToRgb = {1.7166512f, -0.35567078f, -0.25336629f,
                 -0.66668433f, 1.6164813f, 0.01576854f,
                 0.01763986f, -0.04277061f, 0.94210315f},
    .rgbToEncodingXYZ = {0.63695806f, 0.1446169f, 0.16888098f,
                 0.2627002f, 0.67799807f, 0.05930172f,
                 0.0f, 0.02807269f, 1.0609851f},
    .transfer = TransferFunction::Rec2020,
    .videoProfile = {
        "scale=in_range=pc:out_range=tv:out_color_matrix=bt2020",
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
    .encodingXYZToRgb = {2.4934969f, -0.93138361f, -0.40271077f,
                 -0.82948899f, 1.7626641f, 0.02362468f,
                 0.03584583f, -0.07617239f, 0.9568845f},
    .rgbToEncodingXYZ = {0.48657095f, 0.2656677f, 0.19821729f,
                 0.22897457f, 0.69173855f, 0.07928691f,
                 0.0f, 0.04511338f, 1.0439444f},
    .transfer = TransferFunction::SRGB,
    .videoProfile = {
        "scale=in_range=pc:out_range=tv:out_color_matrix=bt709",
        "bt709",
        "smpte432",
        "iec61966-2-1",
        "yuv420p10le"
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
    .encodingXYZToRgb = {3.2409699f, -1.5373832f, -0.49861076f,
                 -0.96924365f, 1.8759675f, 0.04155506f,
                 0.05563008f, -0.20397697f, 1.0569714f},
    .rgbToEncodingXYZ = {0.4123908f, 0.35758433f, 0.18048079f,
                 0.212639f, 0.7151687f, 0.07219231f,
                 0.01933082f, 0.11919478f, 0.95053214f},
    .transfer = TransferFunction::SRGB,
    .videoProfile = {
        "scale=in_range=pc:out_range=tv:out_color_matrix=bt709",
        "bt709",
        "bt709",
        "iec61966-2-1",
        "yuv420p10le"
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

using Matrix = std::array<double, 9>;

Matrix multiplyMatrices(const Matrix& left, const Matrix& right) {
    Matrix result{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            for (std::size_t k = 0; k < 3; ++k)
                result[row*3+column] += left[row*3+k] * right[k*3+column];
    return result;
}

Matrix encodingMatrix(const std::array<float, 9>& values) {
    Matrix result{};
    std::copy(values.begin(), values.end(), result.begin());
    return result;
}

// https://www.itu.int/dms_pub/itu-r/opb/rep/r-rep-bt.2246-2-2012-pdf-e.pdf
// https://cie.co.at/datatable/cie-cone-fundamental-based-spectral-tristimulus-values-2-degree-field-size
const ColourCore::SpectralPrimaryProfile& referencePrimaryProfile() {
    static const auto profile = [] {
        std::array<std::array<float, Colour::CIE2015::kTableSize>, 3> spectra{};
        constexpr std::array<std::size_t, 3> wavelengths{630, 532, 467};
        for (std::size_t channel = 0; channel < spectra.size(); ++channel)
            spectra[channel][wavelengths[channel]-390] = 1.0f;
        const auto unbalanced = ColourCore::makeSpectralPrimaryProfile(spectra[0], spectra[1], spectra[2]);
        const auto gains = ColourCore::projectWithPrimaryProfile({kD65White.X, kD65White.Y, kD65White.Z}, unbalanced);
        const std::array<float, 3> weights{gains.r, gains.g, gains.b};
        for (std::size_t channel = 0; channel < spectra.size(); ++channel)
            spectra[channel][wavelengths[channel]-390] *= weights[channel];
        return ColourCore::makeSpectralPrimaryProfile(spectra[0], spectra[1], spectra[2]);
    }();
    return profile;
}

// The spectral reference is an explicit display model, not a measurement of the user's screen.
// Standard RGB matrices below convert output encodings only; all working XYZ values use CIE 2015.
// https://www.w3.org/TR/css-color-4/#color-conversion-code
const ColourCore::SpectralPrimaryProfile& workingPrimaryProfile(const ColourSpace space) {
    static const auto profiles = [] {
        std::array<ColourCore::SpectralPrimaryProfile, 3> result{};
        const auto& reference = referencePrimaryProfile();
        for (const auto colourSpace : {ColourSpace::Rec2020, ColourSpace::DisplayP3, ColourSpace::SRGB}) {
            const auto& encoding = outputProfileDefinition(colourSpace);
            auto& profile = result[static_cast<std::size_t>(colourSpace)];
            if (colourSpace == ColourSpace::Rec2020) { profile = reference; continue; }
            profile.rgbToXYZ = multiplyMatrices(reference.rgbToXYZ, multiplyMatrices(
                encodingMatrix(kRec2020Definition.encodingXYZToRgb), encodingMatrix(encoding.rgbToEncodingXYZ)));
            profile.xyzToRgb = multiplyMatrices(multiplyMatrices(encodingMatrix(encoding.encodingXYZToRgb),
                encodingMatrix(kRec2020Definition.rgbToEncodingXYZ)), reference.xyzToRgb);
        }
        return result;
    }();
    const auto index = static_cast<std::size_t>(space);
    return profiles[index < profiles.size() ? index : static_cast<std::size_t>(ColourSpace::SRGB)];
}

std::array<float, 3> toEncodingXYZ(const float X, const float Y, const float Z) {
    const auto rgb = ColourCore::projectWithPrimaryProfile({X,Y,Z}, referencePrimaryProfile());
    return multiplyMatrix(kRec2020Definition.rgbToEncodingXYZ, rgb.r, rgb.g, rgb.b);
}

ColourCore::XYZ fromEncodingXYZ(const std::array<float, 3>& xyz) {
    const auto rgb = multiplyMatrix(kRec2020Definition.encodingXYZToRgb, xyz[0], xyz[1], xyz[2]);
    return ColourCore::reconstructWithPrimaryProfile({rgb[0],rgb[1],rgb[2]}, referencePrimaryProfile());
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

float calculateLoudnessDbFromSpectrum(std::span<const float> magnitudes, const ColourCore::SpectrumLayout* layout = nullptr) {
    if (magnitudes.empty()) {
        return kMinLoudnessDb;
    }

    double meanSquare = 0.0;
    for (size_t i = 0; i < magnitudes.size(); ++i) {
        const double magnitude = static_cast<double>(magnitudes[i]);
        const double weight = layout != nullptr && layout->powerWeights.size() == magnitudes.size()
            ? static_cast<double>(layout->powerWeights[i]) : (i == 0 || i + 1 == magnitudes.size() ? 1.0 : 0.5);
        meanSquare += weight * magnitude * magnitude;
    }
    const size_t fftSize = layout != nullptr && layout->fftSize > 0 ? layout->fftSize :
        (magnitudes.size() > 1 ? 2 * (magnitudes.size() - 1) : 1);
    if (fftSize > 2) {
        const double hannMeanSquare = 3.0 / 8.0 * static_cast<double>(fftSize - 1) /
            static_cast<double>(fftSize);
        meanSquare /= hannMeanSquare;
    }
    if (meanSquare <= 0.0) {
        return -std::numeric_limits<float>::infinity();
    }

    return static_cast<float>(10.0 * std::log10(meanSquare));
}

// https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf
float calculateBrightnessDb(std::span<const float> magnitudes, std::span<const float> frequencies,
    float sampleRate, const ColourCore::SpectrumLayout* layout) {
    if (layout && layout->kWeightedMagnitudes.size() >= 2 &&
        std::all_of(layout->kWeightedMagnitudes.begin(), layout->kWeightedMagnitudes.end(),
            [](float value) { return std::isfinite(value) && value >= 0.0f; })) {
        const float gain = std::isfinite(layout->brightnessPowerGain) ? layout->brightnessPowerGain : 1.0f;
        return gain > 0.0f ? -.691f + calculateLoudnessDbFromSpectrum(layout->kWeightedMagnitudes, nullptr) +
            10.0f * std::log10(gain) : -std::numeric_limits<float>::infinity();
    }
    struct Cache {
        float sampleRate = 0.0f;
        std::vector<float> frequencies;
        std::vector<double> weights;
    };
    thread_local Cache cache;
    if (cache.sampleRate != sampleRate || cache.frequencies.size() != frequencies.size() ||
        !std::equal(frequencies.begin(), frequencies.end(), cache.frequencies.begin())) {
        cache.sampleRate = sampleRate;
        cache.frequencies.assign(frequencies.begin(), frequencies.end());
        cache.weights.resize(frequencies.size());
        const auto sections = AudioAnalysis::kWeighting(static_cast<double>(sampleRate));
        for (std::size_t i = 0; i < frequencies.size(); ++i)
            cache.weights[i] = std::isfinite(frequencies[i]) && frequencies[i] >= 0 && frequencies[i] <= sampleRate * .5f ?
                AudioAnalysis::kWeightingPower(sections, static_cast<double>(frequencies[i]), static_cast<double>(sampleRate)) : 0.0;
    }
    double power = 0.0;
    for (std::size_t i = 0; i < magnitudes.size(); ++i) {
        const double weight = layout && layout->powerWeights.size() == magnitudes.size() ?
            static_cast<double>(layout->powerWeights[i]) : (i == 0 || i + 1 == magnitudes.size() ? 1.0 : .5);
        const double magnitude = static_cast<double>(magnitudes[i]);
        power += magnitude * magnitude * weight * cache.weights[i];
    }
    const auto fftSize = layout && layout->fftSize > 0 ? layout->fftSize : 2 * (magnitudes.size() - 1);
    if (fftSize > 2) power /= .375 * static_cast<double>(fftSize - 1) / static_cast<double>(fftSize);
    if (layout && std::isfinite(layout->brightnessPowerGain)) power *= static_cast<double>(std::max(0.0f, layout->brightnessPowerGain));
    return power > 0 ? static_cast<float>(-.691 + 10 * std::log10(power)) : -std::numeric_limits<float>::infinity();
}

float relativeLoudnessStrength(const float loudnessDb) {
    return std::pow(2.0f, (loudnessDb - kPerceptualReferenceLufs) / 10.0f);
}

float clampLoudnessDb(float db) {
    return std::isfinite(db) ? std::clamp(db, kMinLoudnessDb, kMaxLoudnessDb) : kMinLoudnessDb;
}

float normaliseLoudness(const float clampedDb) {
    return (clampedDb - kMinLoudnessDb) / (kMaxLoudnessDb - kMinLoudnessDb);
}

float logisticStrength(const float strength) {
    return strength / (strength + 1.0f);
}

float loudnessToBrightness(const float clampedDb) {
    const float relativeStrength = relativeLoudnessStrength(clampedDb);
    const float logisticValue = logisticStrength(relativeStrength);
    const float logisticMin = logisticStrength(relativeLoudnessStrength(kMinLoudnessDb));
    const float logisticMax = logisticStrength(relativeLoudnessStrength(kMaxLoudnessDb));

    float normalised = 0.0f;
    if (logisticMax - logisticMin > kEpsilonSmall) {
        normalised = (logisticValue - logisticMin) / (logisticMax - logisticMin);
    }
    normalised = std::clamp(normalised, 0.0f, 1.0f);
    return std::pow(normalised, 1.1f);
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
            magnitude <= 0.0f) {
            continue;
        }

        weightedSum += frequency * magnitude;
        totalWeight += magnitude;
    }

    return totalWeight > 0.0f ? weightedSum / totalWeight : 0.0f;
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
            magnitude <= 0.0f) {
            continue;
        }

        const float diff = frequency - centroid;
        spreadSum += magnitude * diff * diff;
        totalWeight += magnitude;
    }

    return totalWeight > 0.0f ? std::sqrt(spreadSum / totalWeight) : 0.0f;
}

// https://librosa.org/doc/0.11.0/generated/librosa.feature.spectral_flatness.html
float calculateSpectralFlatness(std::span<const float> magnitudes,
                               std::span<const float> frequencies) {
    double logSum = 0.0;
    double powerSum = 0.0;
    size_t count = 0;
    bool hasZero = false;
    for (size_t i = 0; i < magnitudes.size() && i < frequencies.size(); ++i) {
        if (!std::isfinite(frequencies[i]) ||
            frequencies[i] < synesthesia::constants::MIN_AUDIO_FREQ ||
            frequencies[i] > synesthesia::constants::MAX_AUDIO_FREQ) {
            continue;
        }
        const double magnitude = static_cast<double>(magnitudes[i]);
        const double power = std::isfinite(magnitude) && magnitude > 0.0 ? magnitude * magnitude : 0.0;
        powerSum += power;
        ++count;
        hasZero = hasZero || power == 0.0;
        if (power > 0.0) {
            logSum += std::log(power);
        }
    }

    if (count == 0 || powerSum == 0.0 || hasZero) {
        return 0.0f;
    }
    const double binCount = static_cast<double>(count);
    return static_cast<float>(std::clamp(std::exp(logSum / binCount) / (powerSum / binCount), 0.0, 1.0));
}

float calculateSpectralRolloff(std::span<const float> magnitudes,
                               std::span<const float> frequencies,
                               const float threshold = 0.85f) {
    if (magnitudes.size() != frequencies.size() || magnitudes.empty()) {
        return 0.0f;
    }

    std::vector<std::pair<float, double>> frequencyPowers;
    frequencyPowers.reserve(magnitudes.size());
    double totalEnergy = 0.0;
    for (size_t i = 0; i < magnitudes.size(); ++i) {
        if (!std::isfinite(magnitudes[i]) || magnitudes[i] <= 0.0f || !std::isfinite(frequencies[i]) ||
            frequencies[i] < synesthesia::constants::MIN_AUDIO_FREQ ||
            frequencies[i] > synesthesia::constants::MAX_AUDIO_FREQ) continue;
        const double magnitude = static_cast<double>(magnitudes[i]);
        const double power = magnitude * magnitude;
        totalEnergy += power;
        frequencyPowers.emplace_back(frequencies[i], power);
    }
    if (totalEnergy <= 0.0) return 0.0f;
    const auto byFrequency = [](const auto& left, const auto& right) { return left.first < right.first; };
    if (!std::is_sorted(frequencyPowers.begin(), frequencyPowers.end(), byFrequency)) {
        std::sort(frequencyPowers.begin(), frequencyPowers.end(), byFrequency);
    }
    const double targetEnergy = totalEnergy * static_cast<double>(threshold);
    double cumulativeEnergy = 0.0;
    for (const auto& [frequency, power] : frequencyPowers) {
        cumulativeEnergy += power;
        if (cumulativeEnergy >= targetEnergy) return frequency;
    }
    return frequencyPowers.back().first;
}

float calculateSpectralCrestFactor(std::span<const float> magnitudes,
                                   const float maxMagnitude,
                                   const float totalEnergy) {
    if (magnitudes.empty() ||
        !std::isfinite(maxMagnitude) ||
        !std::isfinite(totalEnergy) ||
        maxMagnitude <= 0.0f ||
        totalEnergy <= 0.0f) {
        return 1.0f;
    }

    const float rms = std::sqrt(totalEnergy / static_cast<float>(magnitudes.size()));
    if (!std::isfinite(rms) || rms <= 0.0f) {
        return 1.0f;
    }

    return maxMagnitude / rms;
}

// https://colour.readthedocs.io/en/master/_modules/colour/appearance/hke.html
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
    const float du = uPrime - (4.0f * kD65White.X / (kD65White.X + 15.0f + 3.0f * kD65White.Z));
    const float dv = vPrime - (9.0f / (kD65White.X + 15.0f + 3.0f * kD65White.Z));

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

    const float laPow = std::pow(synesthesia::constants::HK_ADAPTING_LUMINANCE, 0.4495f);
    const float kBr = 0.2717f * (6.469f + 6.362f * laPow) / (6.469f + laPow);

    const float inner = 1.0f + (-0.8660f * q + 0.0872f * kBr) * suv + 0.3086f;
    const float vcc = 0.4462f * inner * inner * inner;

    const float clampedVcc = std::clamp(vcc, 0.5f, 3.0f);
    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);
    return 1.0f + clampedStrength * (clampedVcc - 1.0f);
}

}

namespace ColourCore {

WhitePoint D50White() {
    return kD50White;
}

WhitePoint D65White() {
    return kD65White;
}


// https://www.fhwa.dot.gov/Environment/noise/measurement/handbook.cfm
float soundPressureLevel(const float rmsPascals) {
    if (!std::isfinite(rmsPascals) || rmsPascals < 0.0f) return std::numeric_limits<float>::quiet_NaN();
    return static_cast<float>(20.0 * std::log10(static_cast<double>(rmsPascals) / 20e-6));
}

// https://www.color.org/technotes/ICC_TN-06-2025_Recommendations_on_calculation_of_tristimulus_values.pdf
SpectralMeasurement measureSpectrum(std::span<const float> magnitudes, std::span<const float> frequencies,
    ColourPresentation::SpectralContributions* contributions) {
    struct ObserverWeights {
        std::vector<float> frequencies;
        std::vector<std::array<float, 3>> values;
        std::vector<bool> valid;
        std::vector<double> bandPositions;
    };
    thread_local ObserverWeights cached;
    if (cached.frequencies.size() != frequencies.size() ||
        !std::equal(frequencies.begin(), frequencies.end(), cached.frequencies.begin())) {
        cached.frequencies.assign(frequencies.begin(), frequencies.end());
        cached.values.resize(frequencies.size());
        cached.valid.assign(frequencies.size(), false);
        cached.bandPositions.resize(frequencies.size());
        for (std::size_t i = 0; i < frequencies.size(); ++i) {
            const float frequency = frequencies[i];
            if (!std::isfinite(frequency) || frequency < synesthesia::constants::MIN_AUDIO_FREQ ||
                frequency > synesthesia::constants::MAX_AUDIO_FREQ) continue;
            const float wavelength = logFrequencyToWavelength(frequency);
            auto& weights = cached.values[i];
            interpolateCIE(wavelength, weights[0], weights[1], weights[2]);
            cached.valid[i] = true;
            cached.bandPositions[i] = ColourPresentation::contributionBandPosition(static_cast<double>(frequency));
        }
    }
    std::array<double, 3> integrated{};
    std::array<std::array<double, 4>, ColourPresentation::contributionBandCount> bands{};
    if (contributions) contributions->clear();
    double totalWeight = 0.0;
    for (std::size_t i = 0; i < std::min(magnitudes.size(), frequencies.size()); ++i) {
        const double weight = static_cast<double>(magnitudes[i]);
        if (!std::isfinite(weight) || weight <= 0.0 || !cached.valid[i]) continue;
        const auto& values = cached.values[i];
        for (std::size_t component = 0; component < 3; ++component) {
            integrated[component] += weight * static_cast<double>(values[component]);
        }
        if (contributions) {
            const double position = std::clamp(cached.bandPositions[i], 0.0,
                static_cast<double>(ColourPresentation::contributionBandCount - 1));
            const auto left = static_cast<std::size_t>(position);
            const auto right = std::min(left + 1, ColourPresentation::contributionBandCount - 1);
            const double fraction = position - static_cast<double>(left);
            for (std::size_t component = 0; component < 3; ++component) {
                const double value = weight * static_cast<double>(values[component]);
                bands[left][component] += (1.0 - fraction) * value;
                bands[right][component] += fraction * value;
            }
            bands[left][3] += (1.0 - fraction) * weight;
            bands[right][3] += fraction * weight;
        }
        totalWeight += weight;
    }
    if (contributions && totalWeight > 0.0) {
        for (std::size_t i = 0; i < bands.size(); ++i) {
            if (bands[i][3] <= 0.0) continue;
            ColourPresentation::SpectralContribution band;
            band.band = static_cast<std::uint16_t>(i);
            for (std::size_t component = 0; component < 3; ++component)
                band.xyz[component] = static_cast<float>(bands[i][component] / totalWeight);
            band.magnitude = static_cast<float>(bands[i][3]);
            contributions->push_back(band);
        }
    }
    SpectralMeasurement result{};
    if (totalWeight > 0.0) {
        result.xyz = {static_cast<float>(integrated[0] / totalWeight),
            static_cast<float>(integrated[1] / totalWeight), static_cast<float>(integrated[2] / totalWeight)};
        result.available = true;
    }
    return result;
}

XYZ integrateEmission(std::span<const float> emissionAtOneNm) {
    if (emissionAtOneNm.size() != Colour::CIE2015::kTableSize) {
        throw std::invalid_argument("Expected 441 spectral samples from 390 to 830 nm");
    }
    std::array<double, 3> xyz{};
    for (size_t i = 0; i < emissionAtOneNm.size(); ++i) {
        const double emission = static_cast<double>(emissionAtOneNm[i]);
        if (!std::isfinite(emission) || emission < 0.0) {
            throw std::invalid_argument("Spectral emission must be finite and non-negative");
        }
        for (size_t component = 0; component < 3; ++component) {
            xyz[component] += emission * static_cast<double>(Colour::CIE2015::kTable[i][component + 1]);
        }
    }
    return {static_cast<float>(xyz[0]), static_cast<float>(xyz[1]), static_cast<float>(xyz[2])};
}

SpectralPrimaryProfile makeSpectralPrimaryProfile(std::span<const float> redAtOneNm,
                                             std::span<const float> greenAtOneNm,
                                             std::span<const float> blueAtOneNm) {
    const std::array<XYZ, 3> primaries{integrateEmission(redAtOneNm),
        integrateEmission(greenAtOneNm), integrateEmission(blueAtOneNm)};
    const double whiteY = static_cast<double>(primaries[0].Y) + static_cast<double>(primaries[1].Y) + static_cast<double>(primaries[2].Y);
    if (!std::isfinite(whiteY) || whiteY <= 0.0) {
        throw std::invalid_argument("Primary spectra must produce a positive finite white luminance");
    }
    SpectralPrimaryProfile profile{};
    auto& m = profile.rgbToXYZ;
    for (size_t c = 0; c < 3; ++c) {
        m[c] = static_cast<double>(primaries[c].X) / whiteY;
        m[3 + c] = static_cast<double>(primaries[c].Y) / whiteY;
        m[6 + c] = static_cast<double>(primaries[c].Z) / whiteY;
    }
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    const double scale = std::max({std::abs(m[0]) + std::abs(m[1]) + std::abs(m[2]),
        std::abs(m[3]) + std::abs(m[4]) + std::abs(m[5]), std::abs(m[6]) + std::abs(m[7]) + std::abs(m[8])});
    if (!std::isfinite(det) || std::abs(det) <= 1e-12 * scale * scale * scale) {
        throw std::invalid_argument("Primary spectra form a singular or ill-conditioned colour basis");
    }
    auto& inv = profile.xyzToRgb;
    inv = {(m[4]*m[8]-m[5]*m[7])/det, (m[2]*m[7]-m[1]*m[8])/det, (m[1]*m[5]-m[2]*m[4])/det,
           (m[5]*m[6]-m[3]*m[8])/det, (m[0]*m[8]-m[2]*m[6])/det, (m[2]*m[3]-m[0]*m[5])/det,
           (m[3]*m[7]-m[4]*m[6])/det, (m[1]*m[6]-m[0]*m[7])/det, (m[0]*m[4]-m[1]*m[3])/det};
    const double inverseScale = std::max({std::abs(inv[0])+std::abs(inv[1])+std::abs(inv[2]),
        std::abs(inv[3])+std::abs(inv[4])+std::abs(inv[5]), std::abs(inv[6])+std::abs(inv[7])+std::abs(inv[8])});
    if (scale * inverseScale > 1e8) {
        throw std::invalid_argument("Primary spectra form an ill-conditioned colour basis");
    }
    return profile;
}

RGB projectWithPrimaryProfile(const XYZ& xyz, const SpectralPrimaryProfile& profile) {
    const auto& m = profile.xyzToRgb;
    return {static_cast<float>(m[0]*static_cast<double>(xyz.X) + m[1]*static_cast<double>(xyz.Y) + m[2]*static_cast<double>(xyz.Z)),
        static_cast<float>(m[3]*static_cast<double>(xyz.X) + m[4]*static_cast<double>(xyz.Y) + m[5]*static_cast<double>(xyz.Z)),
        static_cast<float>(m[6]*static_cast<double>(xyz.X) + m[7]*static_cast<double>(xyz.Y) + m[8]*static_cast<double>(xyz.Z))};
}

XYZ reconstructWithPrimaryProfile(const RGB& rgb, const SpectralPrimaryProfile& profile) {
    const auto& m = profile.rgbToXYZ;
    return {static_cast<float>(m[0]*static_cast<double>(rgb.r) + m[1]*static_cast<double>(rgb.g) + m[2]*static_cast<double>(rgb.b)),
        static_cast<float>(m[3]*static_cast<double>(rgb.r) + m[4]*static_cast<double>(rgb.g) + m[5]*static_cast<double>(rgb.b)),
        static_cast<float>(m[6]*static_cast<double>(rgb.r) + m[7]*static_cast<double>(rgb.g) + m[8]*static_cast<double>(rgb.b))};
}

void interpolateCIE(const float wavelength, float& X, float& Y, float& Z) {
    Colour::CIE2015::interpolate(wavelength, X, Y, Z);
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
    if (wavelength == maxWavelength) return synesthesia::constants::MIN_AUDIO_FREQ;
    if (wavelength == minWavelength) return synesthesia::constants::MAX_AUDIO_FREQ;
    const float t = (maxWavelength - wavelength) / (maxWavelength - minWavelength);
    const float minLog = std::log2(synesthesia::constants::MIN_AUDIO_FREQ);
    const float maxLog = std::log2(synesthesia::constants::MAX_AUDIO_FREQ);
    return std::clamp(std::pow(2.0f, minLog + t * (maxLog - minLog)),
        synesthesia::constants::MIN_AUDIO_FREQ, synesthesia::constants::MAX_AUDIO_FREQ);
}

float chromaticDominantWavelength(const float X, const float Y, const float Z) {
    // https://ies.org/definitions/dominant-wavelength-of-a-color-stimulus/
    const double sum = static_cast<double>(X) + static_cast<double>(Y) + static_cast<double>(Z);
    if (!std::isfinite(sum) || sum <= 0.0) return 0.0f;
    const double x = static_cast<double>(X) / sum, y = static_cast<double>(Y) / sum;
    const double whiteSum = static_cast<double>(kD65White.X) + static_cast<double>(kD65White.Y) + static_cast<double>(kD65White.Z);
    const double wx = static_cast<double>(kD65White.X) / whiteSum, wy = static_cast<double>(kD65White.Y) / whiteSum;
    const double dx = x - wx, dy = y - wy;
    if (std::hypot(dx, dy) < 1e-7) return 0.0f;
    struct Vertex { double x, y, sum; };
    static const auto locus = [] {
        std::array<Vertex, Colour::CIE2015::kTableSize> result{};
        for (size_t i = 0; i < result.size(); ++i) {
            const auto& row = Colour::CIE2015::kTable[i];
            const double total = static_cast<double>(row[1]) + static_cast<double>(row[2]) + static_cast<double>(row[3]);
            result[i] = {static_cast<double>(row[1]) / total, static_cast<double>(row[2]) / total, total};
        }
        return result;
    }();
    const auto at = [](int wavelength) { return locus[static_cast<size_t>(wavelength - 390)]; };
    for (int wavelength = 390; wavelength <= 830; ++wavelength) {
        const auto v = at(wavelength);
        if (std::hypot(x - v.x, y - v.y) < 1e-12) return static_cast<float>(wavelength);
    }
    double closestDistance = std::numeric_limits<double>::infinity();
    float closestWavelength = 0.0f;
    for (int wavelength = 390; wavelength < 830; ++wavelength) {
        const auto a = at(wavelength), b = at(wavelength + 1);
        const double sx = b.x - a.x, sy = b.y - a.y;
        const double lengthSquared = sx * sx + sy * sy;
        if (lengthSquared == 0.0) continue;
        const double u = std::clamp(((x - a.x) * sx + (y - a.y) * sy) / lengthSquared, 0.0, 1.0);
        const double distance = std::hypot(x - a.x - u * sx, y - a.y - u * sy);
        if (distance < closestDistance - 1e-12) {
            closestDistance = distance;
            closestWavelength = static_cast<float>(wavelength + u * a.sum / ((1.0 - u) * b.sum + u * a.sum));
        }
    }
    if (closestDistance < 5e-8) return closestWavelength;
    const auto intersect = [&](Vertex a, Vertex b, double direction, double& t, double& u) {
        const double sx = b.x - a.x, sy = b.y - a.y;
        const double rx = direction * dx, ry = direction * dy;
        const double determinant = rx * sy - ry * sx;
        if (std::abs(determinant) < 1e-15 * std::hypot(rx, ry)) return false;
        const double px = a.x - wx, py = a.y - wy;
        t = (px * sy - py * sx) / determinant;
        u = (px * ry - py * rx) / determinant;
        return t >= 0.0 && u >= -1e-7 && u <= 1.0 + 1e-7;
    };
    const auto spectralHit = [&](double direction, double& bestT) {
        float wavelengthResult = 0.0f;
        bestT = std::numeric_limits<double>::infinity();
        for (int wavelength = 390; wavelength < 830; ++wavelength) {
            const auto a = at(wavelength), b = at(wavelength + 1);
            double t, u;
            if (intersect(a, b, direction, t, u) && t < bestT) {
                bestT = t;
                u = std::clamp(u, 0.0, 1.0);
                const double fraction = u * a.sum / ((1.0 - u) * b.sum + u * a.sum);
                wavelengthResult = static_cast<float>(wavelength + fraction);
            }
        }
        return wavelengthResult;
    };
    double spectralT;
    const float wavelength = spectralHit(1.0, spectralT);
    double purpleT, purpleU;
    const bool purpleHit = intersect(at(830), at(390), 1.0, purpleT, purpleU);
    if (wavelength > 0.0f && (!purpleHit || spectralT <= purpleT + 1e-7)) return wavelength;
    if (!purpleHit) return 0.0f;
    return -spectralHit(-1.0, spectralT);
}

void relativeXYZtoLab(const float X, const float Y, const float Z, float& L, float& a, float& bValue, const WhitePoint white) {
    const float xr = white.X > 0.0f ? X / white.X : 0.0f;
    const float yr = white.Y > 0.0f ? Y / white.Y : 0.0f;
    const float zr = white.Z > 0.0f ? Z / white.Z : 0.0f;

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

void relativeLabtoXYZ(const float L, const float a, const float bValue, float& X, float& Y, float& Z, const WhitePoint white) {
    const float fY = (L + 16.0f) / 116.0f;
    const float fX = fY + a / 500.0f;
    const float fZ = fY - bValue / 200.0f;

    auto inverse = [](const float value) {
        constexpr float delta = synesthesia::constants::LAB_DELTA;
        constexpr float deltaSquared = delta * delta;
        return value > delta ? std::pow(value, 3.0f) : 3.0f * deltaSquared * (value - 4.0f / 29.0f);
    };

    X = white.X * inverse(fX);
    Y = white.Y * inverse(fY);
    Z = white.Z * inverse(fZ);
}

void XYZtoLab(const float X, const float Y, const float Z, float& L, float& a, float& bValue) {
    // Lab is relative to the same CIE 2015 D65 white as the working tristimulus values.
    relativeXYZtoLab(X, Y, Z, L, a, bValue, kD65White);
}

void LabtoXYZ(const float L, const float a, const float bValue, float& X, float& Y, float& Z) {
    relativeLabtoXYZ(L, a, bValue, X, Y, Z, kD65White);
}

void XYZtoOklab(const float X, const float Y, const float Z, float& L, float& a, float& bValue) {
    // Oklab's published basis is reached through the reference RGB model, rather than reinterpreting working XYZ.
    const auto encoding = toEncodingXYZ(X,Y,Z);
    const auto lms = multiplyMatrix(kEncodingXYZToOklabLms, encoding[0], encoding[1], encoding[2]);
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
    const auto xyz = fromEncodingXYZ(multiplyMatrix(kOklabLmsToEncodingXYZ, l * l * l, m * m * m, s * s * s));
    X = xyz.X;
    Y = xyz.Y;
    Z = xyz.Z;
}

void RGBtoXYZ(const float r, const float g, const float b, float& X, float& Y, float& Z,
              const ColourSpace colourSpace) {
    const auto& definition = outputProfileDefinition(colourSpace);
    const float linearR = decodeTransferValue(definition.transfer, r);
    const float linearG = decodeTransferValue(definition.transfer, g);
    const float linearB = decodeTransferValue(definition.transfer, b);
    const auto xyz = reconstructWithPrimaryProfile({linearR,linearG,linearB}, workingPrimaryProfile(colourSpace));
    X = xyz.X;
    Y = xyz.Y;
    Z = xyz.Z;
}

void XYZtoRGB(const float X, const float Y, const float Z,
              float& r, float& g, float& b,
              const ColourSpace colourSpace,
              const bool applyGamma,
              const bool applyGamutMapping) {
    const auto& definition = outputProfileDefinition(colourSpace);
    const auto& matrix = workingPrimaryProfile(colourSpace).xyzToRgb;
    const std::array<double, 3> xyz{static_cast<double>(X), static_cast<double>(Y), static_cast<double>(Z)};
    const std::array<double, 3> linearRgb{
        matrix[0] * xyz[0] + matrix[1] * xyz[1] + matrix[2] * xyz[2],
        matrix[3] * xyz[0] + matrix[4] * xyz[1] + matrix[5] * xyz[2],
        matrix[6] * xyz[0] + matrix[7] * xyz[1] + matrix[8] * xyz[2]};

    double linearR = linearRgb[0];
    double linearG = linearRgb[1];
    double linearB = linearRgb[2];

    // https://www.w3.org/TR/css-color-4/#interpolation-space
    // https://bottosson.github.io/posts/gamutclipping/#choosing-which-line-to-project-along
    // https://docs.scipy.org/doc/scipy/reference/generated/scipy.special.logsumexp.html
    if (applyGamutMapping) {
        if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z) || Y <= 0.0f) {
            linearR = linearG = linearB = 0.0;
        } else {
            const double luminance = xyz[1];
            const std::array<double,4> distances{0.0, -linearR / luminance, -linearG / luminance, -linearB / luminance};
            const double maximum = *std::max_element(distances.begin(), distances.end());
            if (maximum > 0.0) {
                // Taper the soft maximum to zero at the boundary so in-gamut colours are unchanged and the mapping stays continuous.
                const double boundarySoftness = std::min(.02, maximum);
                double sum = 0.0;
                for (const double distance : distances) sum += std::exp((distance - maximum) / boundarySoftness);
                const double negativeExtent = maximum + boundarySoftness * std::log(sum);
                const double proportion = 1.0 / (1.0 + negativeExtent);
                linearR = luminance + proportion * (linearR - luminance);
                linearG = luminance + proportion * (linearG - luminance);
                linearB = luminance + proportion * (linearB - luminance);
            }
            const double peak = std::max({1.0, linearR, linearG, linearB});
            linearR = std::clamp(linearR / peak, 0.0, 1.0);
            linearG = std::clamp(linearG / peak, 0.0, 1.0);
            linearB = std::clamp(linearB / peak, 0.0, 1.0);
        }
    }

    if (!applyGamma) {
        r = static_cast<float>(linearR);
        g = static_cast<float>(linearG);
        b = static_cast<float>(linearB);
        return;
    }

    r = encodeTransferValue(definition.transfer, static_cast<float>(linearR));
    g = encodeTransferValue(definition.transfer, static_cast<float>(linearG));
    b = encodeTransferValue(definition.transfer, static_cast<float>(linearB));

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

RGB encodeRGB(const RGB& linear, const ColourSpace colourSpace) {
    const auto transfer = outputProfileDefinition(colourSpace).transfer;
    return {encodeTransferValue(transfer, linear.r), encodeTransferValue(transfer, linear.g),
        encodeTransferValue(transfer, linear.b)};
}

const VideoProfile& videoProfileFor(const ColourSpace colourSpace) {
    return outputProfileDefinition(colourSpace).videoProfile;
}

const PngProfile& pngProfileFor(const ColourSpace colourSpace) {
    return pngProfileDefinition(colourSpace);
}

SpectralCharacteristics calculateSpectralCharacteristics(std::span<const float> spectrum, const float sampleRate) {
    SpectralCharacteristics result{};
    if (spectrum.empty() || !std::isfinite(sampleRate) || sampleRate <= 0.0f) {
        return result;
    }
    std::vector<float> frequencies(spectrum.size(), 0.0f);
    std::vector<float> magnitudes(spectrum.size(), 0.0f);
    for (size_t i = 0; i < spectrum.size(); ++i) {
        if (spectrum.size() > 1) {
            frequencies[i] = static_cast<float>(i) * sampleRate /
                (2.0f * static_cast<float>(spectrum.size() - 1));
        }
        magnitudes[i] = std::isfinite(spectrum[i]) && spectrum[i] > 0.0f ? spectrum[i] : 0.0f;
    }
    result.flatness = calculateSpectralFlatness(magnitudes, frequencies);
    result.centroid = calculateSpectralCentroid(magnitudes, frequencies);
    result.spread = calculateSpectralSpread(magnitudes, frequencies, result.centroid);
    result.normalisedSpread = std::min(result.spread / 5000.0f, 1.0f);
    return result;
}

FrameResult analyseSpectrum(std::span<const float> magnitudes,
                            std::span<const float> phases,
                            std::span<const float> frequencies,
                            const float sampleRate,
                            const OutputSettings& outputSettings,
                            const float overrideLoudnessDb,
                            const PhaseAnalysis::PhaseFeatureMetrics* phaseMetrics,
                            const SpectrumLayout* layout,
                            ColourPresentation::SpectralContributions* contributions,
                            const EmissionSpectrum* emissions) {
    FrameResult result{};
    if (contributions) contributions->clear();

    if (magnitudes.empty() || !std::isfinite(sampleRate) || sampleRate <= 0.0f) {
        result.loudnessDb = std::numeric_limits<float>::quiet_NaN();
        result.frameLoudnessDb = std::numeric_limits<float>::quiet_NaN();
        result.brightnessLoudnessDb = kMinLoudnessDb;
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

    for (size_t i = 0; i < cleanMagnitudes.size(); ++i) {
        if (!std::isfinite(effectiveFrequencies[i]) || effectiveFrequencies[i] < 0.0f ||
            effectiveFrequencies[i] > sampleRate * 0.5f) {
            cleanMagnitudes[i] = 0.0f;
        }
    }
    const float computedLoudnessDb = calculateLoudnessDbFromSpectrum(cleanMagnitudes, layout);
    const float brightnessLoudnessDb = clampLoudnessDb(
        calculateBrightnessDb(cleanMagnitudes, effectiveFrequencies, sampleRate, layout));

    for (size_t i = 0; i < cleanMagnitudes.size(); ++i) {
        if (!std::isfinite(effectiveFrequencies[i]) ||
            effectiveFrequencies[i] < synesthesia::constants::MIN_AUDIO_FREQ ||
            effectiveFrequencies[i] > synesthesia::constants::MAX_AUDIO_FREQ) {
            cleanMagnitudes[i] = 0.0f;
        }
    }

    result.spectralCentroid = calculateSpectralCentroid(cleanMagnitudes, effectiveFrequencies);
    result.spectralSpread = calculateSpectralSpread(cleanMagnitudes, effectiveFrequencies, result.spectralCentroid);
    result.spectralFlatness = calculateSpectralFlatness(cleanMagnitudes, effectiveFrequencies);
    result.spectralRolloff = calculateSpectralRolloff(cleanMagnitudes, effectiveFrequencies);

    float maxMagnitude = 0.0f;
    float totalEnergy = 0.0f;
    for (const float magnitude : cleanMagnitudes) {
        maxMagnitude = std::max(maxMagnitude, magnitude);
        totalEnergy += magnitude * magnitude;
    }
    result.spectralCrestFactor = calculateSpectralCrestFactor(cleanMagnitudes, maxMagnitude, totalEnergy);

    result.loudnessIsLufs = std::isfinite(overrideLoudnessDb) && overrideLoudnessDb > -200.0f;
    const float loudnessDb = result.loudnessIsLufs ? overrideLoudnessDb : computedLoudnessDb;
    const float clampedLoudnessDb = clampLoudnessDb(loudnessDb);
    const float loudnessNormalised = std::clamp(normaliseLoudness(clampedLoudnessDb), 0.0f, 1.0f);
    if (std::isfinite(outputSettings.pascalsPerDigitalUnit) && outputSettings.pascalsPerDigitalUnit > 0.0f) {
        result.estimatedSPL = computedLoudnessDb + soundPressureLevel(outputSettings.pascalsPerDigitalUnit);
    }
    if (maxMagnitude == 0.0f) {
        if (std::isfinite(outputSettings.referenceWhiteLuminanceCdM2) && outputSettings.referenceWhiteLuminanceCdM2 > 0.0f) {
            result.luminanceCdM2 = 0.0f;
        }
        result.loudnessDb = loudnessDb;
        result.frameLoudnessDb = computedLoudnessDb;
        result.brightnessLoudnessDb = kMinLoudnessDb;
        return result;
    }


    result.spectral = emissions ? measureSpectrum(emissions->magnitudes, emissions->frequencies, contributions) :
        measureSpectrum(cleanMagnitudes, effectiveFrequencies, contributions);
    const XYZ integratedXYZ = result.spectral.xyz;
    const float brightnessGain = loudnessToBrightness(brightnessLoudnessDb);


    constexpr float spectralExposure = 0.5f;
    const float hkFactor = helmholtzKohlrauschCorrection(
        integratedXYZ.X, integratedXYZ.Y, integratedXYZ.Z);
    const float gain = spectralExposure * brightnessGain / hkFactor;
    result.X = integratedXYZ.X * gain;
    result.Y = integratedXYZ.Y * gain;
    result.Z = integratedXYZ.Z * gain;

    result.loudnessDb = loudnessDb;
    result.frameLoudnessDb = computedLoudnessDb;
    result.brightnessLoudnessDb = brightnessLoudnessDb;
    result.loudnessNormalised = loudnessNormalised;
    result.brightnessNormalised = brightnessGain;
    ColourCore::XYZtoLab(result.X, result.Y, result.Z, result.L, result.a, result.b_comp);
    ColourCore::XYZtoRGB(result.X, result.Y, result.Z,
                         result.r, result.g, result.b,
                         outputSettings.colourSpace,
                         true,
                         outputSettings.applyGamutMapping);

    const auto peak = std::max_element(cleanMagnitudes.begin(), cleanMagnitudes.end());
    result.dominantFrequency = effectiveFrequencies[static_cast<size_t>(peak - cleanMagnitudes.begin())];

    if (phaseMetrics) {
        result.phaseInstabilityNorm = phaseMetrics->instabilityNorm;
        result.phaseCoherenceNorm = phaseMetrics->coherenceNorm;
        result.phaseTransientNorm = phaseMetrics->transientNorm;
    }
    if (std::isfinite(outputSettings.referenceWhiteLuminanceCdM2) && outputSettings.referenceWhiteLuminanceCdM2 > 0.0f &&
        result.r >= 0.0f && result.r <= 1.0f && result.g >= 0.0f && result.g <= 1.0f && result.b >= 0.0f && result.b <= 1.0f) {
        float displayX, displayY, displayZ;
        RGBtoXYZ(result.r, result.g, result.b, displayX, displayY, displayZ, outputSettings.colourSpace);
        result.luminanceCdM2 = std::max(0.0f, displayY) * outputSettings.referenceWhiteLuminanceCdM2;
    }
    result.colourIntensity = result.Y;
    result.dominantWavelength = ColourCore::chromaticDominantWavelength(result.spectral.xyz.X, result.spectral.xyz.Y, result.spectral.xyz.Z);

    if (contributions) {
        for (auto& band : *contributions) {
            for (float& component : band.xyz) component *= gain;
        }
    }
    (void)phases;
    return result;
}

}

#include "batch_exporter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <lodepng.h>

#include "audio/analysis/fft/fft_processor.h"
#include "audio/analysis/presentation/sample_sequence.h"
#include "audio/analysis/presentation/spectral_presentation.h"
#include "colour/colour_core.h"
#include "colour/colour_presentation.h"
#include "resyne/encoding/formats/exporter.h"
#include "resyne/encoding/formats/rsyn_presentation.h"
#include "resyne/recorder/import_helpers.h"

namespace fs = std::filesystem;

namespace CLI {

namespace {

static constexpr int   kDefaultPixelsPerSecond = 20;
static constexpr int   kDefaultHeight          = 800;
static constexpr int   kNumSpectralBands       = 24;
static constexpr int   kNumChromaBins          = 12;

static const std::vector<std::string> kAudioExtensions = {
    ".wav", ".flac", ".mp3", ".mpeg3", ".mpga", ".ogg", ".oga"
};
static const std::vector<std::string> kConditionFeatureNames = [] {
    std::vector<std::string> names = {
        "analysis_lab_l",
        "analysis_lab_a",
        "analysis_lab_b",
        "smoothed_lab_l",
        "smoothed_lab_a",
        "smoothed_lab_b",
        "analysis_oklab_l",
        "analysis_oklab_a",
        "analysis_oklab_b",
        "smoothed_oklab_l",
        "smoothed_oklab_a",
        "smoothed_oklab_b",
        "analysis_spectral_centroid_hz",
        "analysis_spectral_flatness",
        "analysis_spectral_spread_hz",
        "analysis_spectral_rolloff_hz",
        "analysis_spectral_crest_factor",
        "analysis_band_low",
        "analysis_band_mid",
        "analysis_band_high",
        "analysis_band_tilt",
    };
    for (int band = 0; band < kNumSpectralBands; ++band) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "analysis_spectral_band_%02d", band);
        names.emplace_back(buffer);
    }
    names.insert(
        names.end(),
        {
            "analysis_loudness_db",
            "analysis_frame_loudness_db",
            "analysis_brightness_loudness_db",
            "analysis_loudness_normalised",
            "analysis_brightness_normalised",
            "analysis_transient_mix",
            "analysis_phase_instability_norm",
            "analysis_phase_coherence_norm",
            "analysis_phase_transient_norm",
            "smoothing_onset_detected",
            "smoothing_spectral_flux",
            "smoothing_spectral_spread_norm",
            "smoothing_spectral_rolloff_norm",
            "smoothing_spectral_crest_norm",
            "analysis_stereo_left_energy",
            "analysis_stereo_right_energy",
            "analysis_stereo_mid_energy",
            "analysis_stereo_side_energy",
            "analysis_stereo_width",
            "analysis_stereo_balance",
            "analysis_stereo_phase_alignment",
            "analysis_pitch_hz",
            "analysis_pitch_salience",
            "analysis_harmonic_energy_ratio",
            "analysis_inharmonicity",
            "analysis_spectral_contrast_low",
            "analysis_spectral_contrast_mid",
            "analysis_spectral_contrast_high",
        });
    for (int chroma = 0; chroma < kNumChromaBins; ++chroma) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "analysis_chroma_%02d", chroma);
        names.emplace_back(buffer);
    }
    return names;
}();
static const std::vector<std::string> kGlobalFeatureNames = [] {
    std::vector<std::string> names = {
        "global_duration_seconds",
        "global_frame_rate_hz",
        "global_mean_loudness_db",
        "global_std_loudness_db",
        "global_mean_brightness_normalised",
        "global_mean_transient_mix",
        "global_mean_spectral_centroid_hz",
        "global_mean_spectral_rolloff_hz",
        "global_mean_spectral_flatness",
        "global_mean_spectral_crest_factor",
        "global_mean_phase_instability_norm",
        "global_mean_phase_coherence_norm",
        "global_mean_phase_transient_norm",
        "global_mean_stereo_width",
        "global_mean_stereo_balance",
        "global_mean_pitch_hz",
        "global_mean_pitch_salience",
        "global_tempo_bpm",
        "global_tempo_strength",
    };
    for (int chroma = 0; chroma < kNumChromaBins; ++chroma) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "global_chroma_hist_%02d", chroma);
        names.emplace_back(buffer);
    }
    return names;
}();

struct FrameLab {
    float L;
    float a;
    float b;
};

struct BandFeatureSet {
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float tilt = 0.0f;
    std::array<float, kNumSpectralBands> bands{};
};

struct StereoFeatureSet {
    float leftEnergy = 0.0f;
    float rightEnergy = 0.0f;
    float midEnergy = 0.0f;
    float sideEnergy = 0.0f;
    float width = 0.0f;
    float balance = 0.0f;
    float phaseAlignment = 0.0f;
};

struct PitchFeatureSet {
    float pitchHz = 0.0f;
    float pitchSalience = 0.0f;
    float harmonicEnergyRatio = 0.0f;
    float inharmonicity = 0.0f;
    std::array<float, kNumChromaBins> chroma{};
};

struct SpectralContrastSet {
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
};

struct FrameFeatureSet {
    BandFeatureSet bandFeatures{};
    StereoFeatureSet stereoFeatures{};
    PitchFeatureSet pitchFeatures{};
    SpectralContrastSet spectralContrast{};
};

struct TrackGlobalFeatureSet {
    std::vector<float> values;
};

SpectralPresentation::Settings buildBatchSpectralSettings(const RSYNPresentationSettings& settings);

static constexpr float kMinLoudnessDb = -70.0f;
static constexpr float kMaxLoudnessDb =   0.0f;

static float clampLoudnessDb(const float v) {
    if (!std::isfinite(v)) return kMinLoudnessDb;
    return std::clamp(v, kMinLoudnessDb, kMaxLoudnessDb);
}

// Mirrors ColourCore::resolveBrightnessLoudnessDb — kept local to avoid a header dependency.
static float computeBrightnessLoudness(const float frameLoudnessDb,
                                       const float slowLoudnessDb,
                                       const float crestFactor,
                                       float& transientMix) {
    const float clampedFrame = clampLoudnessDb(frameLoudnessDb);
    if (!std::isfinite(slowLoudnessDb)) {
        transientMix = 1.0f;
        return clampedFrame;
    }
    const float clampedSlow = clampLoudnessDb(slowLoudnessDb);
    const float attackDb    = std::max(0.0f, clampedFrame - clampedSlow);
    const float crestMix    = std::clamp((crestFactor - 1.5f) / 4.5f, 0.0f, 1.0f);
    const float attackMix   = std::clamp(attackDb / 12.0f, 0.0f, 1.0f);
    transientMix = attackMix * (0.35f + 0.65f * crestMix);
    return clampedSlow + transientMix * attackDb;
}

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

float safeMean(const std::vector<float>& values) {
    if (values.empty()) {
        return 0.0f;
    }
    const double total = std::accumulate(values.begin(), values.end(), 0.0);
    return static_cast<float>(total / static_cast<double>(values.size()));
}

float safeStdDev(const std::vector<float>& values, const float mean) {
    if (values.size() <= 1) {
        return 0.0f;
    }
    double total = 0.0;
    for (const float value : values) {
        const double diff = static_cast<double>(value) - static_cast<double>(mean);
        total += diff * diff;
    }
    return static_cast<float>(std::sqrt(total / static_cast<double>(values.size())));
}

float weightedMean(const std::vector<float>& values, const std::vector<float>& weights) {
    if (values.empty() || weights.empty() || values.size() != weights.size()) {
        return 0.0f;
    }
    double total = 0.0;
    double totalWeight = 0.0;
    for (size_t index = 0; index < values.size(); ++index) {
        const float value = values[index];
        const float weight = weights[index];
        if (!std::isfinite(value) || !std::isfinite(weight) || weight <= 0.0f) {
            continue;
        }
        total += static_cast<double>(value) * static_cast<double>(weight);
        totalWeight += static_cast<double>(weight);
    }
    if (totalWeight <= 1e-8) {
        return 0.0f;
    }
    return static_cast<float>(total / totalWeight);
}

std::vector<float> resolveFrequencies(const SpectralPresentation::Frame& frame,
                                      const std::vector<float>& sharedMagnitudes) {
    std::vector<float> frequencies = frame.frequencies;
    if (frequencies.size() == sharedMagnitudes.size()) {
        return frequencies;
    }

    frequencies.assign(sharedMagnitudes.size(), 0.0f);
    const float fftSize = sharedMagnitudes.size() > 1
        ? static_cast<float>((sharedMagnitudes.size() - 1) * 2)
        : static_cast<float>(FFTProcessor::FFT_SIZE);
    const float binSize = fftSize > 0.0f ? frame.sampleRate / fftSize : 0.0f;
    for (size_t index = 0; index < frequencies.size(); ++index) {
        frequencies[index] = static_cast<float>(index) * binSize;
    }
    return frequencies;
}

StereoFeatureSet computeStereoFeatures(const AudioColourSample& sample,
                                       const std::vector<float>& frequencies) {
    StereoFeatureSet result{};
    if (sample.magnitudes.size() < 2 || sample.magnitudes[0].empty() || sample.magnitudes[1].empty()) {
        return result;
    }

    const size_t binCount = std::min(sample.magnitudes[0].size(), sample.magnitudes[1].size());
    if (binCount == 0) {
        return result;
    }

    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    double midEnergy = 0.0;
    double sideEnergy = 0.0;
    double phaseAlignment = 0.0;
    double phaseWeight = 0.0;

    const bool hasPhases = sample.phases.size() >= 2 &&
        sample.phases[0].size() >= binCount &&
        sample.phases[1].size() >= binCount;

    for (size_t index = 0; index < binCount; ++index) {
        const float frequency = index < frequencies.size() ? frequencies[index] : 0.0f;
        if (!std::isfinite(frequency) ||
            frequency < FFTProcessor::MIN_FREQ ||
            frequency > std::min(FFTProcessor::MAX_FREQ, sample.sampleRate * 0.5f)) {
            continue;
        }

        const float leftMagnitude = sample.magnitudes[0][index];
        const float rightMagnitude = sample.magnitudes[1][index];
        if (!std::isfinite(leftMagnitude) || !std::isfinite(rightMagnitude) ||
            leftMagnitude <= 0.0f || rightMagnitude <= 0.0f) {
            continue;
        }

        const double left = static_cast<double>(leftMagnitude) * static_cast<double>(leftMagnitude);
        const double right = static_cast<double>(rightMagnitude) * static_cast<double>(rightMagnitude);
        const double mid = std::pow(0.5 * (static_cast<double>(leftMagnitude) + static_cast<double>(rightMagnitude)), 2.0);
        const double side = std::pow(0.5 * (static_cast<double>(leftMagnitude) - static_cast<double>(rightMagnitude)), 2.0);
        leftEnergy += left;
        rightEnergy += right;
        midEnergy += mid;
        sideEnergy += side;

        if (hasPhases) {
            const float leftPhase = sample.phases[0][index];
            const float rightPhase = sample.phases[1][index];
            if (std::isfinite(leftPhase) && std::isfinite(rightPhase)) {
                const double weight = 0.5 * (left + right);
                phaseAlignment += std::cos(static_cast<double>(leftPhase) - static_cast<double>(rightPhase)) * weight;
                phaseWeight += weight;
            }
        }
    }

    const double stereoEnergy = leftEnergy + rightEnergy;
    const double midSideEnergy = midEnergy + sideEnergy;
    if (stereoEnergy > 1e-8) {
        result.leftEnergy = static_cast<float>(leftEnergy / stereoEnergy);
        result.rightEnergy = static_cast<float>(rightEnergy / stereoEnergy);
        result.balance = static_cast<float>((rightEnergy - leftEnergy) / stereoEnergy);
    }
    if (midSideEnergy > 1e-8) {
        result.midEnergy = static_cast<float>(midEnergy / midSideEnergy);
        result.sideEnergy = static_cast<float>(sideEnergy / midSideEnergy);
        result.width = static_cast<float>(sideEnergy / midSideEnergy);
    }
    if (phaseWeight > 1e-8) {
        result.phaseAlignment = static_cast<float>((phaseAlignment / phaseWeight + 1.0) * 0.5);
    }
    return result;
}

PitchFeatureSet computePitchFeatures(const std::vector<float>& sharedMagnitudes,
                                     const std::vector<float>& frequencies) {
    PitchFeatureSet result{};
    if (sharedMagnitudes.empty() || frequencies.size() != sharedMagnitudes.size()) {
        return result;
    }

    constexpr float kReferenceFrequency = 440.0f;
    constexpr int kReferenceMidi = 69;
    constexpr float kMinPitchHz = 30.0f;
    constexpr float kMaxPitchHz = 5000.0f;
    constexpr float kHarmonicTolerance = 0.03f;

    std::unordered_map<int, double> noteEnergy;
    double totalEnergy = 0.0;

    for (size_t index = 0; index < sharedMagnitudes.size(); ++index) {
        const float magnitude = sharedMagnitudes[index];
        const float frequency = frequencies[index];
        if (!std::isfinite(magnitude) || !std::isfinite(frequency) ||
            magnitude <= 0.0f || frequency < FFTProcessor::MIN_FREQ || frequency > FFTProcessor::MAX_FREQ) {
            continue;
        }

        const double energy = static_cast<double>(magnitude) * static_cast<double>(magnitude);
        totalEnergy += energy;

        const double midi = static_cast<double>(kReferenceMidi) +
            12.0 * std::log2(static_cast<double>(frequency) / static_cast<double>(kReferenceFrequency));
        const int nearestMidi = static_cast<int>(std::lround(midi));
        const int chroma = ((nearestMidi % kNumChromaBins) + kNumChromaBins) % kNumChromaBins;
        noteEnergy[nearestMidi] += energy;
        result.chroma[static_cast<size_t>(chroma)] += static_cast<float>(energy);
    }

    if (totalEnergy <= 1e-8 || noteEnergy.empty()) {
        return result;
    }

    int bestMidi = 0;
    double bestEnergy = -1.0;
    for (const auto& [midi, energy] : noteEnergy) {
        if (energy > bestEnergy) {
            bestEnergy = energy;
            bestMidi = midi;
        }
    }

    result.pitchHz = kReferenceFrequency *
        std::pow(2.0f, (static_cast<float>(bestMidi) - static_cast<float>(kReferenceMidi)) / 12.0f);
    if (result.pitchHz < kMinPitchHz || result.pitchHz > kMaxPitchHz) {
        result.pitchHz = 0.0f;
        bestEnergy = 0.0;
    }
    result.pitchSalience = static_cast<float>(bestEnergy / totalEnergy);

    double harmonicEnergy = 0.0;
    double inharmonicity = 0.0;
    if (result.pitchHz > 0.0f) {
        for (size_t index = 0; index < sharedMagnitudes.size(); ++index) {
            const float magnitude = sharedMagnitudes[index];
            const float frequency = frequencies[index];
            if (!std::isfinite(magnitude) || !std::isfinite(frequency) ||
                magnitude <= 0.0f || frequency < FFTProcessor::MIN_FREQ || frequency > FFTProcessor::MAX_FREQ) {
                continue;
            }

            const double energy = static_cast<double>(magnitude) * static_cast<double>(magnitude);
            const double harmonic = std::max(1.0, std::round(static_cast<double>(frequency) / static_cast<double>(result.pitchHz)));
            const double target = harmonic * static_cast<double>(result.pitchHz);
            const double deviation = std::abs(static_cast<double>(frequency) - target) / std::max(target, 1.0);
            if (deviation <= kHarmonicTolerance) {
                harmonicEnergy += energy * (1.0 - deviation / kHarmonicTolerance);
            }
            inharmonicity += energy * std::min(deviation, 1.0);
        }
    }

    result.harmonicEnergyRatio = static_cast<float>(harmonicEnergy / totalEnergy);
    result.inharmonicity = static_cast<float>(inharmonicity / totalEnergy);
    for (float& chroma : result.chroma) {
        chroma = static_cast<float>(static_cast<double>(chroma) / totalEnergy);
    }
    return result;
}

float computeBandContrast(const std::vector<float>& values) {
    if (values.size() < 4) {
        return 0.0f;
    }
    std::vector<float> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const size_t lowIndex = static_cast<size_t>(std::floor(0.1 * static_cast<double>(sorted.size() - 1)));
    const size_t highIndex = static_cast<size_t>(std::floor(0.9 * static_cast<double>(sorted.size() - 1)));
    return sorted[highIndex] - sorted[lowIndex];
}

SpectralContrastSet computeSpectralContrast(const std::vector<float>& sharedMagnitudes,
                                            const std::vector<float>& frequencies) {
    SpectralContrastSet result{};
    std::vector<float> lowBand;
    std::vector<float> midBand;
    std::vector<float> highBand;

    for (size_t index = 0; index < sharedMagnitudes.size() && index < frequencies.size(); ++index) {
        const float magnitude = sharedMagnitudes[index];
        const float frequency = frequencies[index];
        if (!std::isfinite(magnitude) || !std::isfinite(frequency) ||
            magnitude <= 0.0f || frequency < FFTProcessor::MIN_FREQ || frequency > FFTProcessor::MAX_FREQ) {
            continue;
        }
        const float db = 20.0f * std::log10(std::max(magnitude, 1e-6f));
        if (frequency < 250.0f) {
            lowBand.push_back(db);
        } else if (frequency < 4000.0f) {
            midBand.push_back(db);
        } else {
            highBand.push_back(db);
        }
    }

    result.low = computeBandContrast(lowBand);
    result.mid = computeBandContrast(midBand);
    result.high = computeBandContrast(highBand);
    return result;
}

FrameFeatureSet computeFrameFeatures(const AudioMetadata& metadata,
                                     const AudioColourSample& sample) {
    FrameFeatureSet result{};
    const auto frame = SpectralPresentation::SampleSequence::buildFrame(sample);
    const auto sharedMagnitudes = SpectralPresentation::buildSharedMagnitudes(
        frame,
        buildBatchSpectralSettings(metadata.presentationData->settings));
    if (sharedMagnitudes.empty() || frame.sampleRate <= 0.0f) {
        return result;
    }

    const std::vector<float> frequencies = resolveFrequencies(frame, sharedMagnitudes);
    const float nyquist = frame.sampleRate * 0.5f;
    const float maxFrequency = std::min(FFTProcessor::MAX_FREQ, nyquist);
    const float minErb = FFTProcessor::frequencyToERBScale(FFTProcessor::MIN_FREQ);
    const float maxErb = FFTProcessor::frequencyToERBScale(std::max(FFTProcessor::MIN_FREQ, maxFrequency));
    const float erbSpan = std::max(maxErb - minErb, 1e-6f);

    float totalEnergy = 0.0f;
    for (size_t index = 0; index < sharedMagnitudes.size(); ++index) {
        const float magnitude = sharedMagnitudes[index];
        const float frequency = frequencies[index];
        if (!std::isfinite(magnitude) || !std::isfinite(frequency) ||
            magnitude <= 0.0f || frequency < FFTProcessor::MIN_FREQ || frequency > maxFrequency) {
            continue;
        }

        const float energy = magnitude * magnitude;
        totalEnergy += energy;
        if (frequency < 220.0f) {
            result.bandFeatures.low += energy;
        } else if (frequency < 2200.0f) {
            result.bandFeatures.mid += energy;
        } else {
            result.bandFeatures.high += energy;
        }

        const float erb = FFTProcessor::frequencyToERBScale(frequency);
        int bandIndex = static_cast<int>(((erb - minErb) / erbSpan) * static_cast<float>(kNumSpectralBands));
        bandIndex = std::clamp(bandIndex, 0, kNumSpectralBands - 1);
        result.bandFeatures.bands[static_cast<size_t>(bandIndex)] += energy;
    }

    if (totalEnergy > 1e-8f) {
        result.bandFeatures.low /= totalEnergy;
        result.bandFeatures.mid /= totalEnergy;
        result.bandFeatures.high /= totalEnergy;
        const float weightedLow = result.bandFeatures.low + 0.5f * result.bandFeatures.mid;
        const float weightedHigh = result.bandFeatures.high + 0.5f * result.bandFeatures.mid;
        result.bandFeatures.tilt = std::clamp(weightedHigh - weightedLow, -1.0f, 1.0f);
        for (float& value : result.bandFeatures.bands) {
            value /= totalEnergy;
        }
    }

    result.stereoFeatures = computeStereoFeatures(sample, frequencies);
    result.pitchFeatures = computePitchFeatures(sharedMagnitudes, frequencies);
    result.spectralContrast = computeSpectralContrast(sharedMagnitudes, frequencies);
    return result;
}

std::optional<std::pair<float, float>> estimateTempoFromOnsets(const std::vector<float>& onsetEnvelope,
                                                               const float deltaTimeSeconds) {
    if (onsetEnvelope.size() < 8 || !std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0f) {
        return std::nullopt;
    }

    std::vector<float> centered = onsetEnvelope;
    const float mean = safeMean(centered);
    for (float& value : centered) {
        value -= mean;
    }

    const int minLag = std::max(1, static_cast<int>(std::round(60.0 / (240.0 * deltaTimeSeconds))));
    const int maxLag = std::max(minLag + 1, static_cast<int>(std::round(60.0 / (40.0 * deltaTimeSeconds))));
    if (static_cast<size_t>(maxLag) >= centered.size()) {
        return std::nullopt;
    }

    double zeroLag = 0.0;
    for (const float value : centered) {
        zeroLag += static_cast<double>(value) * static_cast<double>(value);
    }
    if (zeroLag <= 1e-8) {
        return std::nullopt;
    }

    double bestCorrelation = -1.0;
    int bestLag = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double correlation = 0.0;
        for (size_t index = static_cast<size_t>(lag); index < centered.size(); ++index) {
            correlation += static_cast<double>(centered[index]) *
                static_cast<double>(centered[index - static_cast<size_t>(lag)]);
        }
        if (correlation > bestCorrelation) {
            bestCorrelation = correlation;
            bestLag = lag;
        }
    }

    const float bpm = 60.0f / (static_cast<float>(bestLag) * deltaTimeSeconds);
    const float strength = static_cast<float>(std::clamp(bestCorrelation / zeroLag, 0.0, 1.0));
    if (!std::isfinite(bpm) || bpm <= 0.0f) {
        return std::nullopt;
    }
    return std::make_pair(bpm, strength);
}

TrackGlobalFeatureSet computeTrackGlobalFeatures(const float durationSeconds,
                                                 const float deltaTimeSeconds,
                                                 const std::vector<float>& loudnesses,
                                                 const std::vector<float>& brightnesses,
                                                 const std::vector<float>& transients,
                                                 const std::vector<float>& centroids,
                                                 const std::vector<float>& rolloffs,
                                                 const std::vector<float>& flatnesses,
                                                 const std::vector<float>& crests,
                                                 const std::vector<float>& phaseInstability,
                                                 const std::vector<float>& phaseCoherence,
                                                 const std::vector<float>& phaseTransient,
                                                 const std::vector<float>& stereoWidths,
                                                 const std::vector<float>& stereoBalances,
                                                 const std::vector<float>& pitchHzs,
                                                 const std::vector<float>& pitchSaliences,
                                                 const std::vector<float>& onsetEnvelope,
                                                 const std::array<double, kNumChromaBins>& chromaAccum) {
    TrackGlobalFeatureSet result{};
    result.values.reserve(kGlobalFeatureNames.size());

    const float loudnessMean = safeMean(loudnesses);
    const float pitchMean = weightedMean(pitchHzs, pitchSaliences);
    const auto tempoEstimate = estimateTempoFromOnsets(onsetEnvelope, deltaTimeSeconds);

    result.values.push_back(durationSeconds);
    result.values.push_back(deltaTimeSeconds > 0.0f ? 1.0f / deltaTimeSeconds : 0.0f);
    result.values.push_back(loudnessMean);
    result.values.push_back(safeStdDev(loudnesses, loudnessMean));
    result.values.push_back(safeMean(brightnesses));
    result.values.push_back(safeMean(transients));
    result.values.push_back(safeMean(centroids));
    result.values.push_back(safeMean(rolloffs));
    result.values.push_back(safeMean(flatnesses));
    result.values.push_back(safeMean(crests));
    result.values.push_back(safeMean(phaseInstability));
    result.values.push_back(safeMean(phaseCoherence));
    result.values.push_back(safeMean(phaseTransient));
    result.values.push_back(safeMean(stereoWidths));
    result.values.push_back(safeMean(stereoBalances));
    result.values.push_back(pitchMean);
    result.values.push_back(safeMean(pitchSaliences));
    result.values.push_back(tempoEstimate.has_value() ? tempoEstimate->first : 0.0f);
    result.values.push_back(tempoEstimate.has_value() ? tempoEstimate->second : 0.0f);

    double chromaTotal = 0.0;
    for (const double value : chromaAccum) {
        chromaTotal += value;
    }
    for (const double value : chromaAccum) {
        result.values.push_back(chromaTotal > 1e-8 ? static_cast<float>(value / chromaTotal) : 0.0f);
    }
    return result;
}

void writeJsonStringArray(std::ostream& stream, const std::vector<std::string>& values) {
    stream << "[";
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << "\"" << values[index] << "\"";
    }
    stream << "]";
}

void writeJsonFloatArray(std::ostream& stream, const std::vector<float>& values) {
    stream << "[";
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        const float value = std::isfinite(values[index]) ? values[index] : 0.0f;
        stream << value;
    }
    stream << "]";
}

bool isAudioFile(const fs::path& path) {
    const std::string ext = toLower(path.extension().string());
    for (const auto& e : kAudioExtensions) {
        if (ext == e) return true;
    }
    return false;
}

RSYNPresentationSettings buildBatchPresentationSettings(const bool disableSmoothing) {
    RSYNPresentationSettings settings{};
    settings.colourSpace = ColourCore::ColourSpace::Rec2020;
    settings.applyGamutMapping = true;
    settings.lowGain = 1.0f;
    settings.midGain = 1.0f;
    settings.highGain = 1.0f;
    settings.smoothingEnabled = !disableSmoothing;
    settings.manualSmoothing = false;
    return settings;
}

SpectralPresentation::Settings buildBatchSpectralSettings(const RSYNPresentationSettings& settings) {
    SpectralPresentation::Settings presentation{};
    presentation.lowGain = settings.lowGain;
    presentation.midGain = settings.midGain;
    presentation.highGain = settings.highGain;
    presentation.colourSpace = settings.colourSpace;
    presentation.applyGamutMapping = settings.applyGamutMapping;
    return presentation;
}

bool buildFrameColours(const std::vector<AudioColourSample>& samples,
                       AudioMetadata& metadata,
                       const bool disableSmoothing,
                       std::vector<FrameLab>& frameColours) {
    frameColours.clear();

    metadata.presentationData = RSYNPresentation::buildPresentationData(
        samples,
        buildBatchPresentationSettings(disableSmoothing));
    if (metadata.presentationData == nullptr || metadata.presentationData->frames.empty()) {
        return false;
    }

    const bool useSmoothedTrack = metadata.presentationData->settings.smoothingEnabled;
    frameColours.reserve(metadata.presentationData->frames.size());
    for (const auto& frame : metadata.presentationData->frames) {
        if (useSmoothedTrack) {
            frameColours.push_back({frame.smoothedLab[0], frame.smoothedLab[1], frame.smoothedLab[2]});
        } else {
            frameColours.push_back({frame.analysis.L, frame.analysis.a, frame.analysis.b_comp});
        }
    }

    metadata.numFrames = frameColours.size();
    return !frameColours.empty();
}

enum class GradientOutputMode {
    png,
    slices,
    both
};

GradientOutputMode parseGradientOutputMode(const std::string& value) {
    const std::string lowered = toLower(value);
    if (lowered == "png") {
        return GradientOutputMode::png;
    }
    if (lowered == "slices") {
        return GradientOutputMode::slices;
    }
    return GradientOutputMode::both;
}

bool exportsPreviewPNG(const GradientOutputMode mode) {
    return mode == GradientOutputMode::png || mode == GradientOutputMode::both;
}

bool exportsRawSlices(const GradientOutputMode mode) {
    return mode == GradientOutputMode::slices || mode == GradientOutputMode::both;
}

bool writeFloat32Npy(const fs::path& outputPath,
                     const std::vector<float>& values,
                     const size_t rows,
                     const size_t cols) {
    std::ofstream stream(outputPath, std::ios::binary);
    if (!stream) {
        return false;
    }

    const std::string shape =
        cols == 1
            ? "(" + std::to_string(rows) + ",)"
            : "(" + std::to_string(rows) + ", " + std::to_string(cols) + ")";
    std::string header =
        "{'descr': '<f4', 'fortran_order': False, 'shape': " + shape + ", }";
    const size_t preambleSize = 10;
    const size_t totalSize = preambleSize + header.size() + 1;
    const size_t padding = (16 - (totalSize % 16)) % 16;
    header.append(padding, ' ');
    header.push_back('\n');

    const char magic[] = "\x93NUMPY";
    stream.write(magic, 6);

    const unsigned char version[2] = {1, 0};
    stream.write(reinterpret_cast<const char*>(version), 2);

    const uint16_t headerLength = static_cast<uint16_t>(header.size());
    const char headerLengthBytes[2] = {
        static_cast<char>(headerLength & 0xff),
        static_cast<char>((headerLength >> 8) & 0xff)
    };
    stream.write(headerLengthBytes, 2);
    stream.write(header.data(), static_cast<std::streamsize>(header.size()));
    stream.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size() * sizeof(float)));
    return stream.good();
}

bool exportConditionSlices(const AudioMetadata& metadata,
                           const std::vector<AudioColourSample>& samples,
                           const fs::path& outputPath,
                           std::vector<float>* globalFeatureValues) {
    if (metadata.presentationData == nullptr || metadata.presentationData->frames.empty() || samples.empty()) {
        return false;
    }
    if (metadata.presentationData->frames.size() != samples.size()) {
        return false;
    }

    const auto& frames = metadata.presentationData->frames;

    // EMA slow-loudness for per-frame transient detection (tau = 200 ms).
    const float deltaTime = (metadata.sampleRate > 0.0f && metadata.hopSize > 0)
        ? static_cast<float>(metadata.hopSize) / metadata.sampleRate
        : 1.0f / 75.0f;
    const float emaAlpha = std::exp(-deltaTime / 0.2f);

    std::vector<float> slowLoudnessDb(frames.size());
    slowLoudnessDb[0] = frames[0].analysis.frameLoudnessDb;
    for (size_t i = 1; i < frames.size(); ++i) {
        slowLoudnessDb[i] = emaAlpha * slowLoudnessDb[i - 1]
                            + (1.0f - emaAlpha) * frames[i].analysis.frameLoudnessDb;
    }

    std::vector<float> values;
    values.reserve(frames.size() * kConditionFeatureNames.size());
    std::vector<float> loudnesses;
    std::vector<float> brightnesses;
    std::vector<float> transients;
    std::vector<float> centroids;
    std::vector<float> rolloffs;
    std::vector<float> flatnesses;
    std::vector<float> crests;
    std::vector<float> phaseInstability;
    std::vector<float> phaseCoherence;
    std::vector<float> phaseTransient;
    std::vector<float> stereoWidths;
    std::vector<float> stereoBalances;
    std::vector<float> pitchHzs;
    std::vector<float> pitchSaliences;
    std::vector<float> onsetEnvelope;
    std::array<double, kNumChromaBins> chromaAccum{};

    for (size_t index = 0; index < frames.size(); ++index) {
        const auto& frame = frames[index];
        const auto featureSet = computeFrameFeatures(metadata, samples[index]);
        const auto& bandFeatures = featureSet.bandFeatures;
        const auto& stereoFeatures = featureSet.stereoFeatures;
        const auto& pitchFeatures = featureSet.pitchFeatures;
        const auto& spectralContrast = featureSet.spectralContrast;

        float transientMix = 0.0f;
        const float brightnessLoudnessDb = computeBrightnessLoudness(
            frame.analysis.frameLoudnessDb,
            slowLoudnessDb[index],
            frame.analysis.spectralCrestFactor,
            transientMix);

        values.push_back(frame.analysis.L);
        values.push_back(frame.analysis.a);
        values.push_back(frame.analysis.b_comp);
        values.push_back(frame.smoothedLab[0]);
        values.push_back(frame.smoothedLab[1]);
        values.push_back(frame.smoothedLab[2]);
        values.push_back(frame.targetOklab[0]);
        values.push_back(frame.targetOklab[1]);
        values.push_back(frame.targetOklab[2]);
        values.push_back(frame.smoothedOklab[0]);
        values.push_back(frame.smoothedOklab[1]);
        values.push_back(frame.smoothedOklab[2]);
        values.push_back(frame.analysis.spectralCentroid);
        values.push_back(frame.analysis.spectralFlatness);
        values.push_back(frame.analysis.spectralSpread);
        values.push_back(frame.analysis.spectralRolloff);
        values.push_back(frame.analysis.spectralCrestFactor);
        values.push_back(bandFeatures.low);
        values.push_back(bandFeatures.mid);
        values.push_back(bandFeatures.high);
        values.push_back(bandFeatures.tilt);
        for (const float bandValue : bandFeatures.bands) {
            values.push_back(bandValue);
        }
        values.push_back(frame.analysis.loudnessDb);
        values.push_back(frame.analysis.frameLoudnessDb);
        values.push_back(brightnessLoudnessDb);
        values.push_back(frame.analysis.loudnessNormalised);
        values.push_back(frame.analysis.brightnessNormalised);
        values.push_back(transientMix);
        values.push_back(frame.analysis.phaseInstabilityNorm);
        values.push_back(frame.analysis.phaseCoherenceNorm);
        values.push_back(frame.analysis.phaseTransientNorm);
        values.push_back(frame.smoothingSignals.onsetDetected ? 1.0f : 0.0f);
        values.push_back(frame.smoothingSignals.spectralFlux);
        values.push_back(frame.smoothingSignals.spectralSpreadNorm);
        values.push_back(frame.smoothingSignals.spectralRolloffNorm);
        values.push_back(frame.smoothingSignals.spectralCrestNorm);
        values.push_back(stereoFeatures.leftEnergy);
        values.push_back(stereoFeatures.rightEnergy);
        values.push_back(stereoFeatures.midEnergy);
        values.push_back(stereoFeatures.sideEnergy);
        values.push_back(stereoFeatures.width);
        values.push_back(stereoFeatures.balance);
        values.push_back(stereoFeatures.phaseAlignment);
        values.push_back(pitchFeatures.pitchHz);
        values.push_back(pitchFeatures.pitchSalience);
        values.push_back(pitchFeatures.harmonicEnergyRatio);
        values.push_back(pitchFeatures.inharmonicity);
        values.push_back(spectralContrast.low);
        values.push_back(spectralContrast.mid);
        values.push_back(spectralContrast.high);
        for (const float chroma : pitchFeatures.chroma) {
            values.push_back(chroma);
        }

        loudnesses.push_back(frame.analysis.loudnessDb);
        brightnesses.push_back(frame.analysis.brightnessNormalised);
        transients.push_back(transientMix);
        centroids.push_back(frame.analysis.spectralCentroid);
        rolloffs.push_back(frame.analysis.spectralRolloff);
        flatnesses.push_back(frame.analysis.spectralFlatness);
        crests.push_back(frame.analysis.spectralCrestFactor);
        phaseInstability.push_back(frame.analysis.phaseInstabilityNorm);
        phaseCoherence.push_back(frame.analysis.phaseCoherenceNorm);
        phaseTransient.push_back(frame.analysis.phaseTransientNorm);
        stereoWidths.push_back(stereoFeatures.width);
        stereoBalances.push_back(stereoFeatures.balance);
        pitchHzs.push_back(pitchFeatures.pitchHz);
        pitchSaliences.push_back(pitchFeatures.pitchSalience);
        onsetEnvelope.push_back(frame.smoothingSignals.spectralFlux);
        for (size_t chromaIndex = 0; chromaIndex < pitchFeatures.chroma.size(); ++chromaIndex) {
            chromaAccum[chromaIndex] += static_cast<double>(pitchFeatures.chroma[chromaIndex]);
        }
    }

    if (globalFeatureValues != nullptr) {
        const float inferredDeltaTime = (metadata.sampleRate > 0.0f && metadata.hopSize > 0)
            ? static_cast<float>(metadata.hopSize) / metadata.sampleRate
            : 0.0f;
        const float inferredDurationSeconds = (metadata.durationSeconds > 0.0)
            ? static_cast<float>(metadata.durationSeconds)
            : inferredDeltaTime * static_cast<float>(frames.size());
        *globalFeatureValues = computeTrackGlobalFeatures(
            inferredDurationSeconds,
            inferredDeltaTime,
            loudnesses,
            brightnesses,
            transients,
            centroids,
            rolloffs,
            flatnesses,
            crests,
            phaseInstability,
            phaseCoherence,
            phaseTransient,
            stereoWidths,
            stereoBalances,
            pitchHzs,
            pitchSaliences,
            onsetEnvelope,
            chromaAccum).values;
    }

    return writeFloat32Npy(
        outputPath,
        values,
        frames.size(),
        kConditionFeatureNames.size());
}

bool writeGradientMetadata(const fs::path& outputPath,
                           const size_t frameCount,
                           const float durationSeconds,
                           const AudioMetadata& metadata,
                           const std::vector<std::string>* featureNames = nullptr,
                           const std::vector<std::string>* globalFeatureNames = nullptr,
                           const std::vector<float>* globalFeatureValues = nullptr) {
    std::ofstream stream(outputPath, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream << "{\n";
    stream << "  \"frame_count\": " << frameCount << ",\n";
    stream << "  \"duration_seconds\": " << durationSeconds << ",\n";
    stream << "  \"sample_rate\": " << metadata.sampleRate << ",\n";
    stream << "  \"hop_size\": " << metadata.hopSize << ",\n";
    stream << "  \"conditioning_schema\": \"synesthesia_frame_condition_array\"";
    if (featureNames != nullptr && !featureNames->empty()) {
        stream << ",\n";
        stream << "  \"feature_names\": ";
        writeJsonStringArray(stream, *featureNames);
    }
    if (globalFeatureNames != nullptr && !globalFeatureNames->empty()) {
        stream << ",\n";
        stream << "  \"global_feature_names\": ";
        writeJsonStringArray(stream, *globalFeatureNames);
    }
    if (globalFeatureValues != nullptr && !globalFeatureValues->empty()) {
        stream << ",\n";
        stream << "  \"global_feature_values\": ";
        writeJsonFloatArray(stream, *globalFeatureValues);
    }
    stream << "\n";
    stream << "}\n";
    return stream.good();
}

bool renderGradientPNG(const std::vector<FrameLab>& frameColours,
                       const std::string& outputPath,
                       int imageWidth,
                       int imageHeight,
                       const ColourCore::ColourSpace colourSpace) {
    if (frameColours.empty()) {
        return false;
    }

    const int numFrames = static_cast<int>(frameColours.size());

    std::vector<unsigned char> pixels(static_cast<size_t>(imageWidth * imageHeight * 3 * 2));

    for (int px = 0; px < imageWidth; ++px) {
        // Map output pixel to fractional frame index
        const float t = (imageWidth > 1)
            ? (static_cast<float>(px) / static_cast<float>(imageWidth - 1))
              * static_cast<float>(numFrames - 1)
            : 0.0f;

        const auto i0   = static_cast<size_t>(t);
        const auto i1   = std::min(i0 + 1, static_cast<size_t>(numFrames - 1));
        const float frac = t - static_cast<float>(i0);

        // Interpolate in Lab space
        const float L    = std::lerp(frameColours[i0].L, frameColours[i1].L, frac);
        const float labA = std::lerp(frameColours[i0].a, frameColours[i1].a, frac);
        const float labB = std::lerp(frameColours[i0].b, frameColours[i1].b, frac);

        // Convert Lab → RGB
        float r = 0.0f, g = 0.0f, b = 0.0f;
        ColourCore::LabtoRGB(L, labA, labB, r, g, b, colourSpace, true);
        ColourPresentation::applyOutputPrecision(r, g, b);

        const auto ru = static_cast<uint16_t>(std::clamp(r, 0.0f, 1.0f) * 65535.0f + 0.5f);
        const auto gu = static_cast<uint16_t>(std::clamp(g, 0.0f, 1.0f) * 65535.0f + 0.5f);
        const auto bu = static_cast<uint16_t>(std::clamp(b, 0.0f, 1.0f) * 65535.0f + 0.5f);

        // Fill entire column
        for (int py = 0; py < imageHeight; ++py) {
            const size_t idx = static_cast<size_t>((py * imageWidth + px) * 6);
            pixels[idx + 0] = static_cast<unsigned char>((ru >> 8) & 0xff);
            pixels[idx + 1] = static_cast<unsigned char>(ru & 0xff);
            pixels[idx + 2] = static_cast<unsigned char>((gu >> 8) & 0xff);
            pixels[idx + 3] = static_cast<unsigned char>(gu & 0xff);
            pixels[idx + 4] = static_cast<unsigned char>((bu >> 8) & 0xff);
            pixels[idx + 5] = static_cast<unsigned char>(bu & 0xff);
        }
    }

    lodepng::State state;
    state.info_raw.colortype = LCT_RGB;
    state.info_raw.bitdepth = 16;
    state.info_png.color.colortype = LCT_RGB;
    state.info_png.color.bitdepth = 16;
    state.encoder.auto_convert = 0;
    // ReSyne's UI and SVG path present these RGB values directly on the display,
    // so batch PNG previews are tagged as sRGB to match the app's visible output.
    const auto& pngProfile = ColourCore::pngProfileFor(ColourCore::ColourSpace::SRGB);
    if (pngProfile.useSrgbChunk) {
        state.info_png.srgb_defined = 1;
        state.info_png.srgb_intent = pngProfile.renderingIntent;
    }
    if (pngProfile.useCicpChunk) {
        state.info_png.cicp_defined = 1;
        state.info_png.cicp_color_primaries = pngProfile.colourPrimaries;
        state.info_png.cicp_transfer_function = pngProfile.transferCharacteristics;
        state.info_png.cicp_matrix_coefficients = pngProfile.matrixCoefficients;
        state.info_png.cicp_video_full_range_flag = pngProfile.fullRangeFlag;
    }

    std::vector<unsigned char> encoded;
    const unsigned error = lodepng::encode(encoded, pixels, static_cast<unsigned>(imageWidth), static_cast<unsigned>(imageHeight), state);
    if (error != 0) {
        return false;
    }
    return lodepng::save_file(encoded, outputPath) == 0;
}

struct ExportResult {
    bool exported = false;
    std::string filename;
    std::string detail;
};

ExportResult exportSingleAudioFile(const fs::path& audioPath,
                                   const fs::path& gradientsDir,
                                   const fs::path& audioOutDir,
                                   bool copyAudio,
                                   int width,
                                   int height,
                                   GradientOutputMode gradientOutputMode,
                                   bool writeConditionSidecar,
                                   bool trueSize,
                                   int analysisHop,
                                   bool disableSmoothing) {
    ExportResult result;
    result.filename = audioPath.filename().string();
    const std::string stem = audioPath.stem().string();

    std::vector<AudioColourSample> samples;
    AudioMetadata metadata{};
    std::string errorMessage;

    const bool imported = ReSyne::ImportHelpers::importAudioFile(
        audioPath.string(),
        ColourCore::ColourSpace::Rec2020,
        true,
        analysisHop,
        1.0f, 1.0f, 1.0f,
        samples, metadata, errorMessage,
        nullptr,
        nullptr
    );

    if (!imported || samples.empty()) {
        result.detail = "skipped";
        if (!errorMessage.empty()) {
            result.detail += " (" + errorMessage + ")";
        }
        return result;
    }

    std::vector<FrameLab> frameColours;
    if (!buildFrameColours(samples, metadata, disableSmoothing, frameColours)) {
        result.detail = "skipped (presentation build failed)";
        return result;
    }

    const float duration = (metadata.durationSeconds > 0.0)
        ? static_cast<float>(metadata.durationSeconds)
        : ((metadata.sampleRate > 0.0f && metadata.hopSize > 0)
        ? static_cast<float>(metadata.numFrames) *
          static_cast<float>(metadata.hopSize) / metadata.sampleRate
        : static_cast<float>(frameColours.size()) * (1024.0f / 44100.0f));

    const int imageWidth = trueSize
        ? std::max(1, static_cast<int>(frameColours.size()))
        : ((width > 0) ? width
                       : std::max(1, static_cast<int>(std::ceil(duration * kDefaultPixelsPerSecond))));
    const int imageHeight = (height > 0) ? height : kDefaultHeight;

    bool exportedPreview = false;
    bool exportedCondition = false;
    std::vector<float> globalFeatureValues;

    const fs::path conditionPath = gradientsDir / (stem + ".cond.npy");
    if (exportsRawSlices(gradientOutputMode) || (exportsPreviewPNG(gradientOutputMode) && writeConditionSidecar)) {
        if (!exportConditionSlices(metadata, samples, conditionPath, &globalFeatureValues)) {
            result.detail = "failed (condition sidecar export error)";
            return result;
        }
        fs::path conditionMetadataPath = conditionPath;
        conditionMetadataPath.replace_extension(".json");
        if (!writeGradientMetadata(
                conditionMetadataPath,
                frameColours.size(),
                duration,
                metadata,
                &kConditionFeatureNames,
                &kGlobalFeatureNames,
                &globalFeatureValues)) {
            result.detail = "failed (condition sidecar metadata error)";
            return result;
        }

        exportedCondition = true;
    }

    if (exportsPreviewPNG(gradientOutputMode)) {
        const fs::path pngPath = gradientsDir / (stem + ".png");
        if (!renderGradientPNG(
                frameColours,
                pngPath.string(),
                imageWidth,
                imageHeight,
                metadata.presentationData->settings.colourSpace)) {
            result.detail = "failed (PNG write error)";
            return result;
        }
        exportedPreview = true;
    }

    result.exported = true;
    if (exportedPreview && exportsRawSlices(gradientOutputMode)) {
        result.detail = "done (PNG + condition slices)";
    } else if (exportedPreview && exportedCondition) {
        result.detail = "done (PNG + condition sidecar)";
    } else if (exportedCondition) {
        result.detail = "done (condition slices)";
    } else {
        result.detail = "done (PNG)";
    }

    if (copyAudio) {
        std::error_code ec;
        const fs::path audioDest = audioOutDir / audioPath.filename();
        fs::copy_file(audioPath, audioDest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            result.detail = "done (warning: audio copy failed - " + ec.message() + ")";
        }
    }

    return result;
}

} // namespace

int BatchExporter::run(const std::string& inputDir,
                       const std::string& outputDir,
                       bool copyAudio,
                       int width,
                       int height,
                       const std::string& gradientFormat,
                       bool writeConditionSidecar,
                       bool trueSize,
                       int numWorkers,
                       int analysisHop,
                       bool disableSmoothing) {
    const GradientOutputMode gradientOutputMode = parseGradientOutputMode(gradientFormat);
    const std::string gradientFormatLowered = toLower(gradientFormat);
    if (gradientFormatLowered != "png" &&
        gradientFormatLowered != "slices" &&
        gradientFormatLowered != "both") {
        std::cerr << "Error: Unsupported gradient format: " << gradientFormat
                  << " (expected png, slices, or both)\n";
        return 1;
    }

    // --- Validate input directory ---
    std::error_code ec;
    if (!fs::exists(inputDir, ec) || !fs::is_directory(inputDir, ec)) {
        std::cerr << "Error: Input directory does not exist or is not a directory: "
                  << inputDir << "\n";
        return 1;
    }

    // --- Collect audio files ---
    std::vector<fs::path> audioFiles;
    for (const auto& entry :
         fs::recursive_directory_iterator(inputDir,
                                          fs::directory_options::skip_permission_denied,
                                          ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!entry.is_regular_file()) continue;
        if (isAudioFile(entry.path())) {
            audioFiles.push_back(entry.path());
        }
    }

    if (audioFiles.empty()) {
        std::cout << "No audio files found in: " << inputDir << "\n";
        return 0;
    }

    std::sort(audioFiles.begin(), audioFiles.end());

    std::cout << "Found " << audioFiles.size() << " audio file(s).\n\n";
    if (trueSize && width > 0) {
        std::cout << "Info: --true-size is enabled; ignoring --width and using analyser frame count.\n\n";
    }
    if (exportsRawSlices(gradientOutputMode) || writeConditionSidecar) {
        std::cout << "Info: condition sidecars use exact analyser frame counts and ignore preview sizing.\n\n";
    }
    const fs::path gradientsDir = copyAudio
        ? fs::path(outputDir) / "gradients"
        : fs::path(outputDir);
    const fs::path audioOutDir  = copyAudio
        ? fs::path(outputDir) / "audio"
        : fs::path{};

    fs::create_directories(gradientsDir, ec);
    if (ec) {
        std::cerr << "Error: Could not create gradients output directory: "
                  << gradientsDir << " (" << ec.message() << ")\n";
        return 1;
    }

    if (copyAudio) {
        fs::create_directories(audioOutDir, ec);
        if (ec) {
            std::cerr << "Error: Could not create audio output directory: "
                      << audioOutDir << " (" << ec.message() << ")\n";
            return 1;
        }
    }

    size_t exported = 0;
    size_t skipped = 0;
    const size_t total = audioFiles.size();
    const size_t workerCount = std::min<size_t>(static_cast<size_t>(std::max(1, numWorkers)), total);

    if (workerCount == 1) {
        for (size_t i = 0; i < total; ++i) {
            ExportResult result = exportSingleAudioFile(
                audioFiles[i],
                gradientsDir,
                audioOutDir,
                copyAudio,
                width,
                height,
                gradientOutputMode,
                writeConditionSidecar,
                trueSize,
                analysisHop,
                disableSmoothing
            );

            std::cout << "[" << (i + 1) << "/" << total << "] "
                      << result.filename << " ... " << result.detail << "\n";

            if (result.exported) {
                ++exported;
            } else {
                ++skipped;
            }
        }
    } else {
        std::cout << "Using " << workerCount << " worker threads.\n\n";
        std::atomic<size_t> nextIndex{0};
        std::atomic<size_t> exportedAtomic{0};
        std::atomic<size_t> skippedAtomic{0};
        std::mutex outputMutex;
        std::vector<std::thread> workers;
        workers.reserve(workerCount);

        for (size_t w = 0; w < workerCount; ++w) {
            workers.emplace_back([&]() {
                while (true) {
                    const size_t idx = nextIndex.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= total) {
                        break;
                    }

                    ExportResult result = exportSingleAudioFile(
                        audioFiles[idx],
                        gradientsDir,
                        audioOutDir,
                        copyAudio,
                        width,
                        height,
                        gradientOutputMode,
                        writeConditionSidecar,
                        trueSize,
                        analysisHop,
                        disableSmoothing
                    );

                    {
                        std::lock_guard<std::mutex> lock(outputMutex);
                        std::cout << "[" << (idx + 1) << "/" << total << "] "
                                  << result.filename << " ... " << result.detail << "\n";
                    }

                    if (result.exported) {
                        exportedAtomic.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        skippedAtomic.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }

        for (auto& worker : workers) {
            worker.join();
        }

        exported = exportedAtomic.load(std::memory_order_relaxed);
        skipped = skippedAtomic.load(std::memory_order_relaxed);
    }

    std::cout << "\n=== Export Complete ===\n";
    std::cout << "Exported: " << exported << " gradient(s)\n";
    if (skipped > 0) {
        std::cout << "Skipped:  " << skipped << " file(s) could not be parsed\n";
    }
    std::cout << "Output:   " << fs::absolute(outputDir) << "\n";

    return 0;
}

} // namespace CLI

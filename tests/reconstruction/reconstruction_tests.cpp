#include "resyne/encoding/audio/wav_encoder.h"
#include "resyne/encoding/reconstruction/spectral_resampling.h"

#include <kiss_fftr.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr size_t kFrameCount = 64;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

float signal(size_t sample, size_t channel) {
    const double time = static_cast<double>(sample) / kSampleRate;
    const double frequency = channel == 0 ? 440.0 : 660.0;
    const double envelope = std::min(1.0, time * 20.0);
    return static_cast<float>(envelope * (0.2 * std::sin(kTwoPi * frequency * time) +
                                         0.08 * std::sin(kTwoPi * frequency * 2.5 * time)));
}

std::vector<SpectralSample> analyse(int fftSize, int hopSize, size_t channels = 1,
                                  float toneFrequency = 0.0f, size_t frameCount = kFrameCount) {
    auto* config = kiss_fftr_alloc(fftSize, 0, nullptr, nullptr);
    require(config != nullptr, "Could not allocate FFT");
    const size_t size = static_cast<size_t>(fftSize);
    std::vector<float> window(size);
    std::vector<kiss_fft_cpx> bins(size / 2 + 1);
    std::vector<SpectralSample> frames(frameCount);
    for (size_t frame = 0; frame < frames.size(); ++frame) {
        auto& spectrum = frames[frame];
        spectrum.magnitudes.resize(channels);
        spectrum.phases.resize(channels);
        spectrum.frequencies.resize(channels);
        spectrum.sampleRate = kSampleRate;
        spectrum.timestamp = static_cast<double>(frame * static_cast<size_t>(hopSize)) / kSampleRate;
        for (size_t channel = 0; channel < channels; ++channel) {
            for (size_t i = 0; i < size; ++i) {
                const double weight = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) /
                                                         static_cast<double>(size - 1));
                const size_t position = frame * static_cast<size_t>(hopSize) + i;
                const float value = toneFrequency > 0.0f
                    ? static_cast<float>(0.2 * std::sin(kTwoPi * toneFrequency *
                                                       static_cast<double>(position) / kSampleRate))
                    : signal(position, channel);
                window[i] = value * static_cast<float>(weight);
            }
            kiss_fftr(config, window.data(), bins.data());
            auto& magnitudes = spectrum.magnitudes[channel];
            auto& phases = spectrum.phases[channel];
            auto& frequencies = spectrum.frequencies[channel];
            magnitudes.resize(bins.size());
            phases.resize(bins.size());
            frequencies.resize(bins.size());
            for (size_t bin = 0; bin < bins.size(); ++bin) {
                const float scale = (bin == 0 || bin + 1 == bins.size()) ? 1.0f : 2.0f;
                magnitudes[bin] = std::hypot(bins[bin].r, bins[bin].i) * scale / static_cast<float>(size);
                phases[bin] = std::atan2(bins[bin].i, bins[bin].r);
                frequencies[bin] = static_cast<float>(bin) * kSampleRate / static_cast<float>(size);
            }
        }
    }
    kiss_fftr_free(config);
    return frames;
}

WAVEncoder::EncodingResult reconstruct(const std::vector<SpectralSample>& frames, int fftSize, int hopSize) {
    auto result = WAVEncoder::reconstructFromSpectralData(frames, kSampleRate, fftSize, hopSize);
    require(result.success, result.errorMessage);
    const size_t expectedLength = ((frames.size() - 1) * static_cast<size_t>(hopSize) +
                                   static_cast<size_t>(fftSize)) * result.numChannels;
    require(result.audioSamples.size() == expectedLength, "Reconstruction changed track length");
    require(std::all_of(result.audioSamples.begin(), result.audioSamples.end(),
                       [](float value) { return std::isfinite(value); }), "Reconstruction contains non-finite samples");
    return result;
}

double rms(const std::vector<float>& audio, size_t margin) {
    double energy = 0.0;
    for (size_t i = margin; i + margin < audio.size(); ++i) {
        energy += static_cast<double>(audio[i]) * audio[i];
    }
    return std::sqrt(energy / static_cast<double>(audio.size() - margin * 2));
}

double frequencyPower(const std::vector<float>& audio, double frequency, size_t margin) {
    double real = 0.0;
    double imaginary = 0.0;
    for (size_t i = margin; i + margin < audio.size(); ++i) {
        const double phase = kTwoPi * frequency * static_cast<double>(i) / kSampleRate;
        real += audio[i] * std::cos(phase);
        imaginary += audio[i] * std::sin(phase);
    }
    return real * real + imaginary * imaginary;
}

void editGreen(std::vector<SpectralSample>& frames, int edit, size_t first, size_t end, size_t channel = 0) {
    for (size_t frame = first; frame < end; ++frame) {
        for (float& frequency : frames[frame].frequencies[channel]) {
            if (frequency <= 0.0f) {
                continue;
            }
            if (edit == 1) {
                frequency *= std::pow(1000.0f, 0.025f);
            } else if (edit == 2) {
                frequency *= std::pow(1000.0f, -0.025f);
            } else {
                const float green = std::clamp(std::log(frequency / 20.0f) / std::log(1000.0f), 0.0f, 1.0f);
                frequency = 20.0f * std::pow(1000.0f, std::pow(green, 0.8f));
            }
        }
    }
}

void run(const std::string& scenario) {
    for (int fftSize : {512, 2048}) {
        for (int divisor : {2, 4, 8}) {
            const int hopSize = fftSize / divisor;
            auto frames = analyse(fftSize, hopSize, scenario == "stereo" ? 2 : 1);
            const auto baseline = reconstruct(frames, fftSize, hopSize);
            const size_t margin = static_cast<size_t>(fftSize) * 2;
            const double baselineRms = rms(baseline.audioSamples, margin);
            if (scenario == "unchanged") {
                SpectralResampling::PhaseState phaseState;
                auto spectrum = SpectralResampling::resampleSpectrum(frames[0].magnitudes[0],
                    frames[0].phases[0], frames[0].frequencies[0], kSampleRate, static_cast<size_t>(fftSize), hopSize, phaseState);
                require(!spectrum.frequencyShifted, "Unedited spectrum was marked as shifted");
                require(spectrum.magnitudes == frames[0].magnitudes[0] && spectrum.phases == frames[0].phases[0],
                        "Unedited spectrum was modified");
                for (size_t i = margin; i + margin < baseline.audioSamples.size(); ++i) {
                    require(std::abs(baseline.audioSamples[i] - signal(i, 0)) < 0.00001f,
                            "Unedited reconstruction changed the waveform");
                }
            } else if (scenario == "green-edits") {
                for (int edit = 1; edit <= 3; ++edit) {
                    auto edited = frames;
                    editGreen(edited, edit, 0, frames.size());
                    const auto audio = reconstruct(edited, fftSize, hopSize).audioSamples;
                    const double level = rms(audio, margin);
                    const auto peak = *std::max_element(audio.begin(), audio.end(),
                        [](float a, float b) { return std::abs(a) < std::abs(b); });
                    std::cout << "FFT " << fftSize << " hop " << hopSize << " edit " << edit
                              << " level ratio " << level / baselineRms << " peak " << std::abs(peak) << '\n';
                    require(level > baselineRms * 0.6, "Green edit caused excessive track attenuation");
                    require(std::abs(peak) < level * 6.0, "Green edit created a peak that reduces the whole track");
                    if (edit < 3) {
                        const double frequency = 440.0 * std::pow(1000.0, edit == 1 ? 0.025 : -0.025);
                        require(frequencyPower(audio, frequency, margin) > frequencyPower(audio, 440.0, margin) * 4.0,
                                "Green edit did not move the tone to the requested frequency");
                    }
                }
            } else if (scenario == "local") {
                editGreen(frames, 3, kFrameCount / 3, kFrameCount * 2 / 3);
                const auto audio = reconstruct(frames, fftSize, hopSize).audioSamples;
                require(std::equal(audio.begin(), audio.begin() + fftSize, baseline.audioSamples.begin()),
                        "Local green edit changed untouched track start");
                require(std::equal(audio.end() - fftSize, audio.end(), baseline.audioSamples.end() - fftSize),
                        "Local green edit changed untouched track end");
            } else if (scenario == "stereo") {
                editGreen(frames, 3, 0, frames.size());
                const auto audio = reconstruct(frames, fftSize, hopSize).audioSamples;
                for (size_t i = 1; i < audio.size(); i += 2) {
                    require(audio[i] == baseline.audioSamples[i], "Green edit changed the other stereo channel");
                }
            } else if (scenario == "silence") {
                editGreen(frames, 3, 0, frames.size());
                for (auto& frame : frames) {
                    std::fill(frame.magnitudes[0].begin(), frame.magnitudes[0].end(), 0.0f);
                }
                const auto audio = reconstruct(frames, fftSize, hopSize).audioSamples;
                require(std::all_of(audio.begin(), audio.end(), [](float value) { return value == 0.0f; }),
                        "Silent green edit generated audio");
            } else if (scenario == "invalid") {
                require(!WAVEncoder::reconstructFromSpectralData(frames, kSampleRate, fftSize, 0).success,
                        "Zero hop size was accepted");
                require(!WAVEncoder::reconstructFromSpectralData(frames, kSampleRate, fftSize, -1).success,
                        "Negative hop size was accepted");
                require(!WAVEncoder::reconstructFromSpectralData(frames, kSampleRate, 1, hopSize).success,
                        "Invalid FFT size was accepted");
            } else if (scenario == "short") {
                for (size_t frameCount : {1u, 2u, 3u}) {
                    auto edited = frames;
                    edited.resize(frameCount);
                    editGreen(edited, 3, 0, edited.size());
                    const auto audio = reconstruct(edited, fftSize, hopSize).audioSamples;
                    require(std::all_of(audio.begin(), audio.end(),
                                       [](float value) { return std::abs(value) < 0.5f; }),
                            "Short green edit created excessive boundary peaks");
                }
            } else if (scenario == "speed-up-quality") {
                for (float frequency : {440.0f, 1000.0f, 8000.0f}) {
                    const auto tone = analyse(fftSize, hopSize, 1, frequency, 128);
                    for (float ratio : {1.05f, 1.1885022f, 1.5f, 2.0f}) {
                        auto edited = tone;
                        for (auto& frame : edited) {
                            for (float& value : frame.frequencies[0]) {
                                value *= ratio;
                            }
                        }
                        const auto audio = reconstruct(edited, fftSize, hopSize).audioSamples;
                        const double centre = static_cast<double>(frequency) * ratio;
                        const size_t qualityMargin = static_cast<size_t>(fftSize) * 4;
                        const double carrier = frequencyPower(audio, centre, qualityMargin);
                        double sidebands = 0.0;
                        for (int offset : {-3, -2, -1, 1, 2, 3}) {
                            const double sideband = centre + static_cast<double>(offset) * kSampleRate / hopSize;
                            if (sideband > 0.0) {
                                sidebands += frequencyPower(audio, sideband, qualityMargin);
                            }
                        }
                        require(sidebands < carrier * 0.0001,
                                "Speed-up produced audible modulation at the FFT frame rate");
                        require(std::abs(rms(audio, qualityMargin) - std::sqrt(0.02)) < 0.003,
                                "Speed-up changed the steady tone level");
                    }
                }
            } else if (scenario == "out-of-band") {
                std::vector<float> magnitudes(static_cast<size_t>(fftSize / 2 + 1), 0.0f);
                std::vector<float> phases(magnitudes.size(), 0.0f);
                std::vector<float> frequencies(magnitudes.size(), 0.0f);
                const size_t bin = magnitudes.size() / 3;
                magnitudes[bin] = 0.2f;
                for (float rate : {22050.0f, kSampleRate}) {
                    frequencies[bin] = rate * 0.75f;
                    SpectralResampling::PhaseState phaseState;
                    const auto spectrum = SpectralResampling::resampleSpectrum(
                        magnitudes, phases, frequencies, rate, static_cast<size_t>(fftSize), hopSize, phaseState);
                    require(std::all_of(spectrum.magnitudes.begin(), spectrum.magnitudes.end(),
                                       [](float value) { return value == 0.0f; }),
                            "Out-of-band green edit restored or folded the original tone");
                }
                magnitudes[bin] = 0.0f;
                magnitudes.back() = 0.2f;
                frequencies.back() = 11035.0f;
                SpectralResampling::PhaseState phaseState;
                const auto spectrum = SpectralResampling::resampleSpectrum(
                    magnitudes, phases, frequencies, 22050.0f, static_cast<size_t>(fftSize), hopSize, phaseState);
                require(std::all_of(spectrum.magnitudes.begin(), spectrum.magnitudes.end(),
                                   [](float value) { return value == 0.0f; }),
                        "Small shift above Nyquist bypassed the out-of-band filter");
            } else {
                throw std::runtime_error("Unknown scenario");
            }
        }
    }
}

}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected one scenario");
        run(argv[1]);
        std::cout << argv[1] << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

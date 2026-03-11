#include "batch_exporter.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "audio/analysis/fft/fft_processor.h"
#include "colour/colour_mapper.h"
#include "resyne/encoding/formats/exporter.h"
#include "resyne/recorder/import_helpers.h"

namespace fs = std::filesystem;

namespace CLI {

namespace {

static constexpr int   kDefaultPixelsPerSecond = 20;
static constexpr int   kDefaultHeight          = 800;

static const std::vector<std::string> kAudioExtensions = {
    ".wav", ".flac", ".mp3", ".mpeg3", ".mpga", ".ogg", ".oga"
};

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool isAudioFile(const fs::path& path) {
    const std::string ext = toLower(path.extension().string());
    for (const auto& e : kAudioExtensions) {
        if (ext == e) return true;
    }
    return false;
}

struct FrameLab {
    float L;
    float a;
    float b;
};

bool renderGradientPNG(const std::vector<FrameLab>& frameColours,
                       const std::string& outputPath,
                       int imageWidth,
                       int imageHeight) {
    if (frameColours.empty()) {
        return false;
    }

    const int numFrames = static_cast<int>(frameColours.size());

    std::vector<uint8_t> pixels(static_cast<size_t>(imageWidth * imageHeight * 3));

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
        ColourMapper::LabtoRGB(L, labA, labB, r, g, b, ColourMapper::ColourSpace::Rec2020);
        r = std::clamp(r, 0.0f, 1.0f);
        g = std::clamp(g, 0.0f, 1.0f);
        b = std::clamp(b, 0.0f, 1.0f);

        const auto ru = static_cast<uint8_t>(r * 255.0f + 0.5f);
        const auto gu = static_cast<uint8_t>(g * 255.0f + 0.5f);
        const auto bu = static_cast<uint8_t>(b * 255.0f + 0.5f);

        // Fill entire column
        for (int py = 0; py < imageHeight; ++py) {
            const size_t idx = static_cast<size_t>((py * imageWidth + px) * 3);
            pixels[idx + 0] = ru;
            pixels[idx + 1] = gu;
            pixels[idx + 2] = bu;
        }
    }

    return stbi_write_png(outputPath.c_str(),
                          imageWidth,
                          imageHeight,
                          3,
                          pixels.data(),
                          imageWidth * 3) != 0;
}

void collapseToMonoSpectrum(const AudioColourSample& sample,
                            std::vector<float>& outMagnitudes,
                            std::vector<float>& outPhases) {
    if (sample.magnitudes.empty() || sample.magnitudes[0].empty()) {
        outMagnitudes.clear();
        outPhases.clear();
        return;
    }

    const size_t numBins = sample.magnitudes[0].size();
    outMagnitudes.assign(numBins, 0.0f);
    outPhases.assign(numBins, 0.0f);

    const size_t numChannels = sample.magnitudes.size();
    for (size_t bin = 0; bin < numBins; ++bin) {
        float sumReal = 0.0f;
        float sumImag = 0.0f;
        size_t usedChannels = 0;

        for (size_t ch = 0; ch < numChannels; ++ch) {
            if (sample.magnitudes[ch].size() <= bin) {
                continue;
            }
            const float mag = sample.magnitudes[ch][bin];
            if (!std::isfinite(mag) || mag <= 0.0f) {
                continue;
            }

            float phase = 0.0f;
            if (ch < sample.phases.size() && sample.phases[ch].size() > bin) {
                phase = sample.phases[ch][bin];
            }

            sumReal += mag * std::cos(phase);
            sumImag += mag * std::sin(phase);
            ++usedChannels;
        }

        if (usedChannels == 0) {
            continue;
        }

        const float invChannels = 1.0f / static_cast<float>(usedChannels);
        sumReal *= invChannels;
        sumImag *= invChannels;

        outMagnitudes[bin] = std::sqrt(sumReal * sumReal + sumImag * sumImag);
        outPhases[bin] = std::atan2(sumImag, sumReal);
    }
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
                                   bool trueSize,
                                   bool noSmoothing,
                                   bool noMelWeighting) {
    ExportResult result;
    result.filename = audioPath.filename().string();
    const std::string stem = audioPath.stem().string();

    std::vector<AudioColourSample> samples;
    AudioMetadata metadata{};
    std::string errorMessage;

    const bool imported = ReSyne::ImportHelpers::importAudioFile(
        audioPath.string(),
        2.2f,
        ColourMapper::ColourSpace::Rec2020,
        true,
        1.0f, 1.0f, 1.0f,
        samples, metadata, errorMessage,
        nullptr,
        nullptr,
        !noSmoothing,
        !noMelWeighting
    );

    if (!imported || samples.empty()) {
        result.detail = "skipped";
        if (!errorMessage.empty()) {
            result.detail += " (" + errorMessage + ")";
        }
        return result;
    }

    std::vector<FrameLab> frameColours;
    frameColours.reserve(samples.size());

    std::vector<float> mags;
    std::vector<float> phases;
    for (const auto& sample : samples) {
        if (sample.magnitudes.empty()) {
            continue;
        }

        collapseToMonoSpectrum(sample, mags, phases);
        if (mags.empty()) {
            continue;
        }

        const auto colour = ColourMapper::spectrumToColour(
            mags, phases, {}, sample.sampleRate, 2.2f,
            ColourMapper::ColourSpace::Rec2020, true,
            std::numeric_limits<float>::quiet_NaN()
        );

        frameColours.push_back({colour.L, colour.a, colour.b_comp});
    }

    if (frameColours.empty()) {
        result.detail = "skipped (no colour data)";
        return result;
    }

    const float duration = (metadata.sampleRate > 0.0f && metadata.hopSize > 0)
        ? static_cast<float>(metadata.numFrames) *
          static_cast<float>(metadata.hopSize) / metadata.sampleRate
        : static_cast<float>(frameColours.size()) * (1024.0f / 44100.0f);

    const int imageWidth = trueSize
        ? std::max(1, static_cast<int>(frameColours.size()))
        : ((width > 0) ? width
                       : std::max(1, static_cast<int>(std::ceil(duration * kDefaultPixelsPerSecond))));
    const int imageHeight = (height > 0) ? height : kDefaultHeight;

    const fs::path pngPath = gradientsDir / (stem + ".png");
    if (!renderGradientPNG(frameColours, pngPath.string(), imageWidth, imageHeight)) {
        result.detail = "failed (PNG write error)";
        return result;
    }

    result.exported = true;
    result.detail = "done";

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
                       bool trueSize,
                       bool noSmoothing,
                       bool noMelWeighting,
                       int numWorkers) {
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
    if (noSmoothing) {
        std::cout << "Info: analyser smoothing is disabled for this export.\n\n";
    }
    if (noMelWeighting) {
        std::cout << "Info: analyser mel weighting is disabled for this export.\n\n";
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
                trueSize,
                noSmoothing,
                noMelWeighting
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
                        trueSize,
                        noSmoothing,
                        noMelWeighting
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

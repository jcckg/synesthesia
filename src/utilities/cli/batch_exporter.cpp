#include "batch_exporter.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <lodepng.h>

#include "audio/analysis/fft/fft_processor.h"
#include "colour/colour_mapper.h"
#include "resyne/encoding/formats/exporter.h"
#include "resyne/recorder/colour_cache_utils.h"
#include "resyne/recorder/import_helpers.h"

namespace fs = std::filesystem;

namespace CLI {

namespace {

static constexpr int   kDefaultPixelsPerSecond = 20;
static constexpr int   kDefaultHeight          = 800;
static constexpr float kDefaultGamma           = 0.8f;

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

bool exportRawGradientSlices(const std::vector<FrameLab>& frameColours,
                             const fs::path& outputPath) {
    std::vector<float> values;
    values.reserve(frameColours.size() * 3);

    for (const auto& frameColour : frameColours) {
        values.push_back(frameColour.L);
        values.push_back(frameColour.a);
        values.push_back(frameColour.b);
    }

    return writeFloat32Npy(outputPath, values, frameColours.size(), 3);
}

bool writeGradientMetadata(const fs::path& outputPath,
                           const size_t frameCount,
                           const float durationSeconds,
                           const AudioMetadata& metadata) {
    std::ofstream stream(outputPath, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream << "{\n";
    stream << "  \"frame_count\": " << frameCount << ",\n";
    stream << "  \"duration_seconds\": " << durationSeconds << ",\n";
    stream << "  \"sample_rate\": " << metadata.sampleRate << ",\n";
    stream << "  \"hop_size\": " << metadata.hopSize << "\n";
    stream << "}\n";
    return stream.good();
}

bool renderGradientPNG(const std::vector<FrameLab>& frameColours,
                       const std::string& outputPath,
                       int imageWidth,
                       int imageHeight) {
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
        ColourMapper::LabtoRGB(L, labA, labB, r, g, b, ColourMapper::ColourSpace::Rec2020);
        r = std::clamp(r, 0.0f, 1.0f);
        g = std::clamp(g, 0.0f, 1.0f);
        b = std::clamp(b, 0.0f, 1.0f);

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
                                   bool writeLabSidecar,
                                   bool trueSize,
                                   int analysisHop) {
    ExportResult result;
    result.filename = audioPath.filename().string();
    const std::string stem = audioPath.stem().string();

    std::vector<AudioColourSample> samples;
    AudioMetadata metadata{};
    std::string errorMessage;

    const bool imported = ReSyne::ImportHelpers::importAudioFile(
        audioPath.string(),
        kDefaultGamma,
        ColourMapper::ColourSpace::Rec2020,
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
    frameColours.reserve(samples.size());
    ReSyne::RecorderColourCache::CacheSettings settings{};
    settings.gamma = kDefaultGamma;
    settings.colourSpace = ColourMapper::ColourSpace::Rec2020;
    settings.gamutMapping = true;
    settings.smoothingEnabled = false;
    settings.smoothingAmount = 0.0f;

    for (size_t index = 0; index < samples.size(); ++index) {
        const auto entry = ReSyne::RecorderColourCache::computeSampleColour(
            samples[index],
            settings,
            index > 0 ? &samples[index - 1] : nullptr);
        frameColours.push_back({entry.labL, entry.labA, entry.labB});
    }

    if (frameColours.empty()) {
        result.detail = "skipped (no colour data)";
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
    bool exportedSlices = false;
    bool exportedSidecar = false;

    const fs::path slicePath = gradientsDir / (stem + ".lab.npy");
    if (exportsRawSlices(gradientOutputMode) || (exportsPreviewPNG(gradientOutputMode) && writeLabSidecar)) {
        if (!exportRawGradientSlices(frameColours, slicePath)) {
            result.detail = "failed (slice export error)";
            return result;
        }
        fs::path metadataPath = slicePath;
        metadataPath.replace_extension(".json");
        if (!writeGradientMetadata(metadataPath, frameColours.size(), duration, metadata)) {
            result.detail = "failed (slice metadata error)";
            return result;
        }
        exportedSidecar = true;
        exportedSlices = exportsRawSlices(gradientOutputMode);
    }

    if (exportsPreviewPNG(gradientOutputMode)) {
        const fs::path pngPath = gradientsDir / (stem + ".png");
        if (!renderGradientPNG(frameColours, pngPath.string(), imageWidth, imageHeight)) {
            result.detail = "failed (PNG write error)";
            return result;
        }
        exportedPreview = true;
    }

    result.exported = true;
    if (exportedPreview && exportedSlices) {
        result.detail = "done (PNG + slices)";
    } else if (exportedPreview && exportedSidecar) {
        result.detail = "done (PNG + Lab sidecar)";
    } else if (exportedSlices) {
        result.detail = "done (slices)";
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
                       bool writeLabSidecar,
                       bool trueSize,
                       int numWorkers,
                       int analysisHop) {
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
    if (exportsRawSlices(gradientOutputMode) || writeLabSidecar) {
        std::cout << "Info: Lab sidecars use exact analyser frame counts and ignore preview sizing.\n\n";
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
                writeLabSidecar,
                trueSize,
                analysisHop
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
                        writeLabSidecar,
                        trueSize,
                        analysisHop
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

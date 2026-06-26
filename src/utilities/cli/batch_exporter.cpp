#include "batch_exporter.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <lodepng.h>

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

static const std::vector<std::string> kAudioExtensions = {
    ".wav", ".flac", ".mp3", ".mpeg3", ".mpga", ".ogg", ".oga"
};

struct FrameLab {
    float L;
    float a;
    float b;
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

    result.exported = true;
    result.detail = "done (PNG)";

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

ExportResult exportSingleRsynFile(const fs::path& audioPath,
                                  const fs::path& rsynDir,
                                  int analysisHop) {
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

    RSYNExportOptions options{};
    options.presentationSettings = buildBatchPresentationSettings(false);

    const fs::path rsynPath = rsynDir / (stem + ".rsyn");
    if (!SequenceExporter::exportToRsyn(rsynPath.string(), samples, metadata, options)) {
        result.detail = "failed (.rsyn write error)";
        return result;
    }

    result.exported = true;
    result.detail = "done (.rsyn)";
    return result;
}

} // namespace

int BatchExporter::runGradients(const std::string& inputDir,
                                const std::string& outputDir,
                                bool copyAudio,
                                int width,
                                int height,
                                bool trueSize,
                                int numWorkers,
                                int analysisHop,
                                bool disableSmoothing) {
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

int BatchExporter::runRsyn(const std::string& inputDir,
                           const std::string& outputDir,
                           int numWorkers,
                           int analysisHop) {
    std::error_code ec;
    const fs::path inputPath(inputDir);
    if (!fs::exists(inputPath, ec)) {
        std::cerr << "Error: Input path does not exist: "
                  << inputDir << "\n";
        return 1;
    }

    std::vector<fs::path> audioFiles;
    if (fs::is_regular_file(inputPath, ec)) {
        if (!isAudioFile(inputPath)) {
            std::cerr << "Error: Input file is not a supported audio file: "
                      << inputDir << "\n";
            return 1;
        }
        audioFiles.push_back(inputPath);
    } else if (fs::is_directory(inputPath, ec)) {
        for (const auto& entry :
             fs::recursive_directory_iterator(inputPath,
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
    } else {
        std::cerr << "Error: Input path is neither an audio file nor a directory: "
                  << inputDir << "\n";
        return 1;
    }

    if (audioFiles.empty()) {
        std::cout << "No audio files found in: " << inputDir << "\n";
        return 0;
    }

    std::sort(audioFiles.begin(), audioFiles.end());
    fs::create_directories(outputDir, ec);
    if (ec) {
        std::cerr << "Error: Could not create .rsyn output directory: "
                  << outputDir << " (" << ec.message() << ")\n";
        return 1;
    }

    std::cout << "Found " << audioFiles.size() << " audio file(s).\n\n";

    size_t exported = 0;
    size_t skipped = 0;
    const size_t total = audioFiles.size();
    const size_t workerCount = std::min<size_t>(static_cast<size_t>(std::max(1, numWorkers)), total);
    const fs::path rsynDir = fs::path(outputDir);

    if (workerCount == 1) {
        for (size_t i = 0; i < total; ++i) {
            ExportResult result = exportSingleRsynFile(audioFiles[i], rsynDir, analysisHop);

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

                    ExportResult result = exportSingleRsynFile(audioFiles[idx], rsynDir, analysisHop);

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
    std::cout << "Exported: " << exported << " .rsyn asset(s)\n";
    if (skipped > 0) {
        std::cout << "Skipped:  " << skipped << " file(s) could not be parsed\n";
    }
    std::cout << "Output:   " << fs::absolute(outputDir) << "\n";

    return 0;
}

} // namespace CLI

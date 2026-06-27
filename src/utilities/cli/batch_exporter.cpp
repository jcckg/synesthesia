#include "batch_exporter.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif

#include <lodepng.h>

#include "colour/colour_core.h"
#include "colour/colour_presentation.h"
#include "resyne/decoding/audio_decoder.h"
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

std::string formatDuration(const double seconds) {
    const auto totalSeconds = static_cast<long long>(std::max(0.0, std::round(seconds)));
    const long long minutes = totalSeconds / 60;
    const long long remainder = totalSeconds % 60;
    std::ostringstream stream;
    stream << minutes << ":" << std::setw(2) << std::setfill('0') << remainder;
    return stream.str();
}

std::string formatBytes(const std::uint64_t bytes) {
    constexpr double kGb = 1000.0 * 1000.0 * 1000.0;
    constexpr double kMb = 1000.0 * 1000.0;
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(bytes >= 1000000000ULL ? 2 : 1)
           << (bytes >= 1000000000ULL ? static_cast<double>(bytes) / kGb : static_cast<double>(bytes) / kMb)
           << (bytes >= 1000000000ULL ? " GB" : " MB");
    return stream.str();
}

std::uint64_t gigabytesToBytes(const double value) {
    if (value <= 0.0 || !std::isfinite(value)) {
        return 0;
    }
    constexpr double kGb = 1000.0 * 1000.0 * 1000.0;
    return static_cast<std::uint64_t>(std::ceil(value * kGb));
}

std::uint64_t directoryRsynSize(const fs::path& directory) {
    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) {
        return 0;
    }

    std::uint64_t total = 0;
    for (const auto& entry : fs::recursive_directory_iterator(directory, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (toLower(entry.path().extension().string()) != ".rsyn") {
            continue;
        }
        total += static_cast<std::uint64_t>(entry.file_size(ec));
        if (ec) {
            ec.clear();
        }
    }
    return total;
}

struct WorkerLine {
    std::string filename = "idle";
    std::string stage = "waiting";
    float progress = 0.0f;
    bool active = false;
};

class ProgressDisplay {
public:
    ProgressDisplay(const std::size_t workerCount, const std::size_t totalFiles)
        : workers_(workerCount), totalFiles_(totalFiles) {
#if defined(__APPLE__) || defined(__linux__)
        interactive_ = isatty(STDOUT_FILENO) != 0;
#endif
    }

    ~ProgressDisplay() {
        close();
    }

    void setWorker(const std::size_t workerIndex,
                   const std::string& filename,
                   const std::string& stage,
                   const float progress,
                   const bool active) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (workerIndex >= workers_.size()) {
            return;
        }
        const WorkerLine& previous = workers_[workerIndex];
        if (interactive_ &&
            previous.active == active &&
            previous.filename == filename &&
            previous.stage == stage &&
            std::fabs(previous.progress - std::clamp(progress, 0.0f, 1.0f)) < 0.005f) {
            return;
        }
        workers_[workerIndex].filename = filename;
        workers_[workerIndex].stage = stage;
        workers_[workerIndex].progress = std::clamp(progress, 0.0f, 1.0f);
        workers_[workerIndex].active = active;
        if (interactive_) {
            renderLocked();
        }
    }

    void setSummary(const std::size_t completed,
                    const std::size_t exported,
                    const std::size_t skipped,
                    const std::uint64_t bytes,
                    const std::uint64_t cutoffBytes,
                    const bool cutoffReached) {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_ = completed;
        exported_ = exported;
        skipped_ = skipped;
        bytes_ = bytes;
        cutoffBytes_ = cutoffBytes;
        cutoffReached_ = cutoffReached;
        if (interactive_) {
            renderLocked();
        }
    }

    void log(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        clearLocked();
        std::cout << message << "\n";
        if (interactive_) {
            renderLocked();
        }
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        clearLocked();
    }

private:
    std::size_t lineCount() const {
        return workers_.size() + 1;
    }

    void reserveLocked() {
        if (!interactive_ || reserved_) {
            return;
        }

        const std::size_t lines = lineCount();
        for (std::size_t line = 1; line < lines; ++line) {
            std::cout << "\n";
        }
        if (lines > 1) {
            std::cout << "\033[" << (lines - 1) << "F";
        }
        renderedLines_ = lines;
        reserved_ = true;
    }

    void moveToTopLocked() {
        if (!reserved_ || renderedLines_ == 0) {
            return;
        }
        if (renderedLines_ > 1) {
            std::cout << "\033[" << (renderedLines_ - 1) << "F";
        } else {
            std::cout << "\r";
        }
    }

    static std::string truncateMiddle(const std::string& value, const std::size_t limit) {
        if (value.size() <= limit) {
            return value;
        }
        if (limit <= 3) {
            return value.substr(0, limit);
        }
        const std::size_t head = (limit - 3) / 2;
        const std::size_t tail = limit - 3 - head;
        return value.substr(0, head) + "..." + value.substr(value.size() - tail);
    }

    static std::string bar(const float progress) {
        constexpr std::size_t width = 28;
        const std::size_t filled = static_cast<std::size_t>(std::round(std::clamp(progress, 0.0f, 1.0f) * static_cast<float>(width)));
        std::string output;
        output.reserve(width + 2);
        output.push_back('[');
        for (std::size_t index = 0; index < width; ++index) {
            output.push_back(index < filled ? '#' : '-');
        }
        output.push_back(']');
        return output;
    }

    void clearLocked() {
        if (!reserved_ || renderedLines_ == 0) {
            return;
        }

        moveToTopLocked();
        for (std::size_t line = 0; line < renderedLines_; ++line) {
            std::cout << "\r\033[2K";
            if (line + 1 < renderedLines_) {
                std::cout << "\033[1B";
            }
        }
        moveToTopLocked();
        std::cout << "\r";
        reserved_ = false;
        renderedLines_ = 0;
    }

    void renderLocked() {
        const bool wasReserved = reserved_;
        reserveLocked();
        if (wasReserved) {
            moveToTopLocked();
        }

        std::ostringstream summary;
        summary << "Progress " << completed_ << "/" << totalFiles_
                << " | exported " << exported_
                << " | skipped " << skipped_
                << " | output " << formatBytes(bytes_);
        if (cutoffBytes_ > 0) {
            summary << "/" << formatBytes(cutoffBytes_);
            if (cutoffReached_) {
                summary << " cutoff reached";
            }
        }
        std::cout << "\r\033[2K" << summary.str();

        for (std::size_t index = 0; index < workers_.size(); ++index) {
            const WorkerLine& worker = workers_[index];
            const float percent = worker.active ? worker.progress * 100.0f : 0.0f;
            std::cout << "\033[1B\r\033[2K"
                      << "worker " << (index + 1) << " "
                      << bar(worker.active ? worker.progress : 0.0f) << " "
                      << std::setw(5) << std::fixed << std::setprecision(1) << percent << "% "
                      << worker.stage << " "
                      << truncateMiddle(worker.filename, 42);
        }
        renderedLines_ = lineCount();
        std::cout.flush();
    }

    std::mutex mutex_;
    std::vector<WorkerLine> workers_;
    std::size_t totalFiles_ = 0;
    std::size_t completed_ = 0;
    std::size_t exported_ = 0;
    std::size_t skipped_ = 0;
    std::uint64_t bytes_ = 0;
    std::uint64_t cutoffBytes_ = 0;
    bool cutoffReached_ = false;
    bool interactive_ = false;
    bool reserved_ = false;
    std::size_t renderedLines_ = 0;
};

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
    std::uint64_t bytesDelta = 0;
    std::uint64_t outputBytes = 0;
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
                                  int analysisHop,
                                  int compressionLevel,
                                  std::size_t workerIndex,
                                  ProgressDisplay* progressDisplay) {
    ExportResult result;
    result.filename = audioPath.filename().string();
    const std::string stem = audioPath.stem().string();
    const fs::path rsynPath = rsynDir / (stem + ".rsyn");

    std::error_code ec;
    const std::uint64_t previousSize = fs::exists(rsynPath, ec)
        ? static_cast<std::uint64_t>(fs::file_size(rsynPath, ec))
        : 0;

    std::vector<AudioColourSample> samples;
    AudioMetadata metadata{};
    std::string errorMessage;
    if (progressDisplay != nullptr) {
        progressDisplay->setWorker(workerIndex, result.filename, "analysing", 0.0f, true);
    }

    const bool imported = ReSyne::ImportHelpers::importAudioFile(
        audioPath.string(),
        ColourCore::ColourSpace::Rec2020,
        true,
        analysisHop,
        1.0f, 1.0f, 1.0f,
        samples, metadata, errorMessage,
        [progressDisplay, workerIndex, filename = result.filename](const float value) {
            if (progressDisplay != nullptr) {
                progressDisplay->setWorker(workerIndex, filename, "analysing", value * 0.72f, true);
            }
        },
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
    options.compressionLevel = compressionLevel;

    if (progressDisplay != nullptr) {
        progressDisplay->setWorker(workerIndex, result.filename, "writing", 0.72f, true);
    }

    if (!SequenceExporter::exportToRsyn(
            rsynPath.string(),
            samples,
            metadata,
            options,
            [progressDisplay, workerIndex, filename = result.filename](const float value) {
                if (progressDisplay != nullptr) {
                    progressDisplay->setWorker(workerIndex, filename, "writing", 0.72f + value * 0.28f, true);
                }
            })) {
        result.detail = "failed (.rsyn write error)";
        return result;
    }

    const std::uint64_t finalSize = fs::exists(rsynPath, ec)
        ? static_cast<std::uint64_t>(fs::file_size(rsynPath, ec))
        : 0;
    result.outputBytes = finalSize;
    result.bytesDelta = finalSize > previousSize ? finalSize - previousSize : 0;
    result.exported = true;
    result.detail = "done (.rsyn, " + formatBytes(finalSize) + ")";
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
                           int analysisHop,
                           int compressionLevel,
                           double storageCutoffGb,
                           double skipLargeSeconds,
                           bool randomiseOrder) {
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
    if (randomiseOrder) {
        std::random_device device;
        std::mt19937 rng(device());
        std::shuffle(audioFiles.begin(), audioFiles.end(), rng);
    }

    fs::create_directories(outputDir, ec);
    if (ec) {
        std::cerr << "Error: Could not create .rsyn output directory: "
                  << outputDir << " (" << ec.message() << ")\n";
        return 1;
    }

    std::cout << "Found " << audioFiles.size() << " audio file(s).\n\n";
    const int clampedCompressionLevel = std::clamp(compressionLevel, 0, 9);
    if (clampedCompressionLevel > 0) {
        std::cout << "RSYN compression: level " << clampedCompressionLevel << "\n\n";
    }
    if (randomiseOrder) {
        std::cout << "Order: randomised\n\n";
    }
    if (skipLargeSeconds > 0.0) {
        std::cout << "Skipping tracks longer than " << formatDuration(skipLargeSeconds) << "\n\n";
    }

    const std::uint64_t cutoffBytes = gigabytesToBytes(storageCutoffGb);
    std::uint64_t initialOutputBytes = directoryRsynSize(outputDir);
    if (cutoffBytes > 0) {
        std::cout << "Storage cutoff: " << formatBytes(cutoffBytes)
                  << " (current .rsyn output: " << formatBytes(initialOutputBytes) << ")\n\n";
        if (initialOutputBytes >= cutoffBytes) {
            std::cout << "Storage cutoff already reached; no files processed.\n";
            std::cout << "Output:   " << fs::absolute(outputDir) << "\n";
            return 0;
        }
    }

    const size_t total = audioFiles.size();
    const size_t workerCount = std::min<size_t>(static_cast<size_t>(std::max(1, numWorkers)), total);
    const fs::path rsynDir = fs::path(outputDir);
    std::cout << "Using " << workerCount << " worker thread" << (workerCount == 1 ? "" : "s") << ".\n\n";

    ProgressDisplay progress(workerCount, total);
    std::atomic<size_t> nextIndex{0};
    std::atomic<size_t> completedAtomic{0};
    std::atomic<size_t> exportedAtomic{0};
    std::atomic<size_t> skippedAtomic{0};
    std::atomic<std::uint64_t> outputBytesAtomic{initialOutputBytes};
    std::atomic<bool> stopScheduling{false};
    std::atomic<bool> cutoffAnnounced{false};

    progress.setSummary(0, 0, 0, initialOutputBytes, cutoffBytes, false);

    auto updateSummary = [&]() {
        progress.setSummary(
            completedAtomic.load(std::memory_order_relaxed),
            exportedAtomic.load(std::memory_order_relaxed),
            skippedAtomic.load(std::memory_order_relaxed),
            outputBytesAtomic.load(std::memory_order_relaxed),
            cutoffBytes,
            cutoffBytes > 0 && outputBytesAtomic.load(std::memory_order_relaxed) >= cutoffBytes);
    };

    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
        workers.emplace_back([&, workerIndex]() {
            while (true) {
                if (stopScheduling.load(std::memory_order_relaxed)) {
                    break;
                }
                if (cutoffBytes > 0 && outputBytesAtomic.load(std::memory_order_relaxed) >= cutoffBytes) {
                    stopScheduling.store(true, std::memory_order_relaxed);
                    break;
                }

                const size_t idx = nextIndex.fetch_add(1, std::memory_order_relaxed);
                if (idx >= total) {
                    break;
                }

                const fs::path& audioPath = audioFiles[idx];
                const std::string filename = audioPath.filename().string();
                progress.setWorker(workerIndex, filename, "probing", 0.0f, true);

                bool shouldProcess = true;
                if (skipLargeSeconds > 0.0) {
                    double durationSeconds = 0.0;
                    std::string probeError;
                    if (AudioDecoding::probeDurationSeconds(audioPath.string(), durationSeconds, probeError)) {
                        if (durationSeconds > skipLargeSeconds) {
                            shouldProcess = false;
                            skippedAtomic.fetch_add(1, std::memory_order_relaxed);
                            completedAtomic.fetch_add(1, std::memory_order_relaxed);
                            updateSummary();
                            progress.log("[" + std::to_string(idx + 1) + "/" + std::to_string(total) + "] " +
                                         filename + " ... skipped (duration " + formatDuration(durationSeconds) +
                                         " > " + formatDuration(skipLargeSeconds) + ")");
                            progress.setWorker(workerIndex, "idle", "waiting", 0.0f, false);
                            updateSummary();
                        }
                    } else {
                        progress.log("[" + std::to_string(idx + 1) + "/" + std::to_string(total) + "] " +
                                     filename + " ... warning (could not probe duration: " + probeError + ")");
                    }
                }

                if (!shouldProcess) {
                    continue;
                }

                ExportResult result = exportSingleRsynFile(
                    audioPath,
                    rsynDir,
                    analysisHop,
                    clampedCompressionLevel,
                    workerIndex,
                    &progress);

                if (result.exported) {
                    exportedAtomic.fetch_add(1, std::memory_order_relaxed);
                    const std::uint64_t newTotal = outputBytesAtomic.fetch_add(result.bytesDelta, std::memory_order_relaxed) + result.bytesDelta;
                    if (cutoffBytes > 0 && newTotal >= cutoffBytes) {
                        stopScheduling.store(true, std::memory_order_relaxed);
                        if (!cutoffAnnounced.exchange(true, std::memory_order_relaxed)) {
                            updateSummary();
                            progress.log("Storage cutoff reached at " + formatBytes(newTotal) + "; no new files will be scheduled.");
                        }
                    }
                } else {
                    skippedAtomic.fetch_add(1, std::memory_order_relaxed);
                }

                completedAtomic.fetch_add(1, std::memory_order_relaxed);
                progress.log("[" + std::to_string(idx + 1) + "/" + std::to_string(total) + "] " +
                             result.filename + " ... " + result.detail);
                progress.setWorker(workerIndex, "idle", "waiting", 0.0f, false);
                updateSummary();
            }
            progress.setWorker(workerIndex, "idle", "done", 0.0f, false);
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    updateSummary();
    progress.close();

    const size_t exported = exportedAtomic.load(std::memory_order_relaxed);
    const size_t skipped = skippedAtomic.load(std::memory_order_relaxed);
    const size_t completed = completedAtomic.load(std::memory_order_relaxed);

    std::cout << "\n=== Export Complete ===\n";
    std::cout << "Exported: " << exported << " .rsyn asset(s)\n";
    if (skipped > 0) {
        std::cout << "Skipped:  " << skipped << " file(s)\n";
    }
    if (completed < total) {
        std::cout << "Unprocessed: " << (total - completed) << " file(s) left after cutoff\n";
    }
    std::cout << "Output size: " << formatBytes(outputBytesAtomic.load(std::memory_order_relaxed)) << "\n";
    std::cout << "Output:   " << fs::absolute(outputDir) << "\n";

    return 0;
}

} // namespace CLI

#include "resyne/encoding/formats/format_mp4.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "colour/colour_mapper.h"
#include "ui/smoothing/smoothing.h"
#include "utilities/video/ffmpeg_locator.h"

namespace ReSyne::Encoding::Video {

namespace {
namespace fs = std::filesystem;

constexpr double kSmoothUpdateFactor = 1.2;
constexpr int kDefaultResolution = 1080;
constexpr int kDefaultFps = 60;
constexpr int kMinResolution = 320;
constexpr int kMaxResolution = 7680;
constexpr int kMinFps = 1;
constexpr int kMaxFps = 240;

struct RGB {
    float r;
    float g;
    float b;
};

struct TempFile {
    fs::path path;
    ~TempFile() {
        if (!path.empty()) {
            std::error_code ec;
            fs::remove(path, ec);
        }
    }
};

#ifdef _WIN32
FILE* openPipe(const std::string& command) {
    return _popen(command.c_str(), "wb");
}
int closePipe(FILE* handle) {
    return _pclose(handle);
}
#else
FILE* openPipe(const std::string& command) {
    return popen(command.c_str(), "w");
}
int closePipe(FILE* handle) {
    return pclose(handle);
}
#endif

inline uint8_t toByte(float value) {
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

class ColourTimelineSampler {
public:
    ColourTimelineSampler(const std::vector<AudioColourSample>& source,
                          float gamma,
                          ColourMapper::ColourSpace colourSpace,
                          bool gamut,
                          float smoothingAmount,
                          double frameInterval)
        : samples_(source),
          gamma_(gamma),
          colourSpace_(colourSpace),
          gamut_(gamut),
          smoother_(8.0f, 1.0f, 0.3f),
          frameInterval_(frameInterval) {
        smoother_.setSmoothingAmount(std::clamp(smoothingAmount, 0.0f, 1.0f));
        timestamps_.reserve(samples_.size());
        for (const auto& sample : samples_) {
            timestamps_.push_back(sample.timestamp);
        }
        if (!timestamps_.empty() && timestamps_.front() > 0.0) {
            startTime_ = timestamps_.front();
        }
    }

    RGB colourAt(double timeSeconds) {
        timeSeconds = std::max(timeSeconds, 0.0);
        const AudioColourSample& sample = sampleForTime(timeSeconds);
        const auto colour = ColourMapper::spectrumToColour(
            sample.magnitudes,
            sample.phases,
            sample.sampleRate,
            gamma_,
            colourSpace_,
            gamut_);

        RGB rgb{std::clamp(colour.r, 0.0f, 1.0f),
                std::clamp(colour.g, 0.0f, 1.0f),
                std::clamp(colour.b, 0.0f, 1.0f)};

        if (!initialised_) {
            smoother_.reset(rgb.r, rgb.g, rgb.b);
            initialised_ = true;
            return rgb;
        }

        smoother_.setTargetColour(rgb.r, rgb.g, rgb.b);
        smoother_.update(static_cast<float>(static_cast<float>(frameInterval_) * static_cast<float>(kSmoothUpdateFactor)));
        float outR, outG, outB;
        smoother_.getCurrentColour(outR, outG, outB);
        return RGB{outR, outG, outB};
    }

    const AudioColourSample& sampleForTime(double timeSeconds) const {
        if (samples_.empty()) {
            return fallbackSample_;
        }
        if (timestamps_.empty()) {
            return samples_.front();
        }

        const double targetTime = timeSeconds + startTime_;
        auto it = std::lower_bound(timestamps_.begin(), timestamps_.end(), targetTime);
        if (it == timestamps_.end()) {
            return samples_.back();
        }
        const size_t index = static_cast<size_t>(std::distance(timestamps_.begin(), it));
        return samples_[index];
    }

private:
    const std::vector<AudioColourSample>& samples_;
    std::vector<double> timestamps_;
    float gamma_;
    ColourMapper::ColourSpace colourSpace_;
    bool gamut_;
    SpringSmoother smoother_;
    double frameInterval_;
    bool initialised_ = false;
    double startTime_ = 0.0;
    AudioColourSample fallbackSample_{};
};

void paintColourFrame(std::vector<uint8_t>& buffer, int width, int height, const RGB& colour) {
    const uint8_t r = toByte(colour.r);
    const uint8_t g = toByte(colour.g);
    const uint8_t b = toByte(colour.b);
    const int lineStride = width * 3;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = buffer.data() + static_cast<size_t>(y) * static_cast<size_t>(lineStride);
        for (int x = 0; x < width; ++x) {
            uint8_t* pixel = row + static_cast<size_t>(x) * 3;
            pixel[0] = r;
            pixel[1] = g;
            pixel[2] = b;
        }
    }
}

void paintGradientFrame(std::vector<uint8_t>& buffer, int width, int height,
                       const std::vector<RGB>& history, const RGB& backgroundColour) {
    const int lineStride = width * 3;
    const bool hasHistory = !history.empty();
    const size_t historySize = history.size();

    for (int x = 0; x < width; ++x) {
        RGB colour = backgroundColour;

        if (hasHistory && historySize > 0) {
            const float t = historySize > 1
                ? static_cast<float>(x) / static_cast<float>(width - 1)
                : 0.0f;
            const float historyPos = t * static_cast<float>(historySize - 1);

            if (historySize == 1) {
                colour = history[0];
            } else {
                const size_t idx0 = std::min(static_cast<size_t>(historyPos), historySize - 1);
                const size_t idx1 = std::min(idx0 + 1, historySize - 1);
                const float frac = historyPos - static_cast<float>(idx0);

                const RGB& c0 = history[idx0];
                const RGB& c1 = history[idx1];
                colour.r = c0.r * (1.0f - frac) + c1.r * frac;
                colour.g = c0.g * (1.0f - frac) + c1.g * frac;
                colour.b = c0.b * (1.0f - frac) + c1.b * frac;
            }
        }

        const uint8_t r = toByte(colour.r);
        const uint8_t g = toByte(colour.g);
        const uint8_t b = toByte(colour.b);

        for (int y = 0; y < height; ++y) {
            uint8_t* row = buffer.data() + static_cast<size_t>(y) * static_cast<size_t>(lineStride);
            uint8_t* pixel = row + static_cast<size_t>(x) * 3;
            pixel[0] = r;
            pixel[1] = g;
            pixel[2] = b;
        }
    }
}

double computeDuration(const std::vector<AudioColourSample>& samples,
                       const AudioMetadata& metadata) {
    if (!samples.empty()) {
        const double lastTimestamp = samples.back().timestamp;
        if (lastTimestamp > 0.0) {
            return lastTimestamp;
        }
    }
    if (metadata.sampleRate > 0.0f && metadata.hopSize > 0 && !samples.empty()) {
        const double frames = static_cast<double>(samples.size());
        return frames * static_cast<double>(metadata.hopSize) /
               static_cast<double>(metadata.sampleRate);
    }
    return std::max(0.1, static_cast<double>(samples.size()));
}

std::string determineVideoEncoder() {
    if (const char* overrideCodec = std::getenv("RESYNE_FFMPEG_VIDEO_CODEC"); overrideCodec && *overrideCodec) {
        return overrideCodec;
    }

    const auto& locator = Utilities::Video::FFmpegLocator::instance();
    if (locator.supportsEncoder("libx264")) {
        return "libx264";
    }

#if defined(__APPLE__)
    if (locator.supportsEncoder("h264_videotoolbox")) {
        return "h264_videotoolbox";
    }
#endif

    if (locator.supportsEncoder("mpeg4")) {
        return "mpeg4";
    }

    return "mpeg4";
}

void appendEncoderParameters(std::ostringstream& stream, const std::string& encoder) {
    if (encoder == "libx264") {
        stream << " -pix_fmt yuv420p -preset medium -crf 18";
    } else if (encoder == "h264_videotoolbox") {
        stream << " -pix_fmt yuv420p -b:v 12M -maxrate 12M -bufsize 24M -allow_sw 1";
    } else {
        stream << " -pix_fmt yuv420p -q:v 3";
    }
}

std::string buildFFmpegCommand(const std::string& ffmpegPath,
                               const fs::path& audioPath,
                               const std::string& outputPath,
                               const fs::path& stderrPath,
                               int width,
                               int height,
                               int fps) {
    const std::string videoEncoder = determineVideoEncoder();

    std::ostringstream oss;
    oss << '"' << ffmpegPath << '"'
        << " -y -loglevel error"
        << " -f rawvideo -pixel_format rgb24"
        << " -video_size " << width << 'x' << height
        << " -framerate " << fps
        << " -i -"
        << " -i " << '"' << audioPath.string() << '"'
        << " -map 0:v:0 -map 1:a:0"
        << " -c:v " << videoEncoder;

    appendEncoderParameters(oss, videoEncoder);

    oss << " -c:a aac -b:a 192k -movflags +faststart -avoid_negative_ts make_zero"
        << " \"" << outputPath << "\"";

#ifdef _WIN32
    oss << " 2>\"" << stderrPath.string() << "\"";
#else
    oss << " 2>\"" << stderrPath.string() << "\"";
#endif

    return oss.str();
}

bool renderVideo(const std::string& ffmpegCommand,
                 const fs::path& stderrPath,
                 int width,
                 int height,
                 int fps,
                 const std::vector<AudioColourSample>& samples,
                 const ExportOptions& options,
                 const std::function<void(float)>& progress,
                 double duration,
                 std::string& errorMessage,
                 std::vector<RGB>* gradientHistory = nullptr) {
    const int totalFrames = std::max(1, static_cast<int>(std::ceil(duration * static_cast<double>(fps))));
    const int lineStride = width * 3;
    const size_t frameBytes = static_cast<size_t>(lineStride) * static_cast<size_t>(height);
    std::vector<uint8_t> frame(frameBytes, 0);

    ColourTimelineSampler sampler(samples,
                                  options.gamma,
                                  options.colourSpace,
                                  options.applyGamutMapping,
                                  options.smoothingAmount,
                                  1.0 / static_cast<double>(fps));

    if (gradientHistory) {
        gradientHistory->reserve(static_cast<size_t>(totalFrames));
    }

    FILE* pipe = openPipe(ffmpegCommand);
    if (!pipe) {
        errorMessage = "Unable to start FFmpeg";
        return false;
    }

    constexpr size_t kPipeBufferSize = 4 * 1024 * 1024;
    std::vector<char> pipeBuffer(kPipeBufferSize);
    setvbuf(pipe, pipeBuffer.data(), _IOFBF, kPipeBufferSize);

    const float renderStart = 0.15f;
    const float renderSpan = gradientHistory ? 0.35f : 0.75f;

    for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex) {
        const double time = std::min(duration, static_cast<double>(frameIndex) / static_cast<double>(fps));
        const RGB colour = sampler.colourAt(time);

        if (gradientHistory) {
            gradientHistory->push_back(colour);
        }

        paintColourFrame(frame, width, height, colour);

        if (fwrite(frame.data(), 1, frame.size(), pipe) != frame.size()) {
            errorMessage = "Failed to stream video frame to FFmpeg";
            closePipe(pipe);
            return false;
        }

        if (progress) {
            const float fraction = static_cast<float>(frameIndex + 1) / static_cast<float>(totalFrames);
            progress(renderStart + renderSpan * fraction);
        }
    }

    const int exitCode = closePipe(pipe);
    if (exitCode != 0) {
        errorMessage = "FFmpeg exited with an error (code " + std::to_string(exitCode) + ")";

        std::error_code ec;
        if (fs::exists(stderrPath, ec) && fs::file_size(stderrPath, ec) > 0) {
            std::ifstream stderrFile(stderrPath);
            if (stderrFile.is_open()) {
                std::string line;
                std::string stderrContent;
                while (std::getline(stderrFile, line) && stderrContent.size() < 500) {
                    if (!stderrContent.empty()) {
                        stderrContent += " | ";
                    }
                    stderrContent += line;
                }
                if (!stderrContent.empty()) {
                    errorMessage += ": " + stderrContent;
                }
            }
        }
        return false;
    }

    return true;
}

std::string getGradientFilename(const std::string& originalPath) {
    const fs::path path(originalPath);
    const fs::path parent = path.parent_path();
    const fs::path stem = path.stem();
    const fs::path extension = path.extension();

    fs::path gradientPath = parent / (stem.string() + "_gradient" + extension.string());
    return gradientPath.string();
}

}


bool exportToMP4(const std::string& outputPath,
                 const std::vector<AudioColourSample>& samples,
                 const AudioMetadata& metadata,
                 const ExportOptions& options,
                 const std::function<void(float)>& progress,
                 std::string& errorMessage) {
    if (samples.empty()) {
        errorMessage = "No samples available for video export";
        return false;
    }
    if (options.ffmpegExecutable.empty()) {
        errorMessage = "FFmpeg executable path is empty";
        return false;
    }

    const int width = options.width > 0 ? options.width : kDefaultResolution;
    const int height = options.height > 0 ? options.height : kDefaultResolution;
    const int fps = options.frameRate > 0 ? options.frameRate : kDefaultFps;

    if (width < kMinResolution || width > kMaxResolution) {
        errorMessage = "Width must be between " + std::to_string(kMinResolution) +
                       " and " + std::to_string(kMaxResolution) + " pixels";
        return false;
    }
    if (height < kMinResolution || height > kMaxResolution) {
        errorMessage = "Height must be between " + std::to_string(kMinResolution) +
                       " and " + std::to_string(kMaxResolution) + " pixels";
        return false;
    }

    if (fps < kMinFps || fps > kMaxFps) {
        errorMessage = "Frame rate must be between " + std::to_string(kMinFps) +
                       " and " + std::to_string(kMaxFps) + " fps";
        return false;
    }

    if (progress) {
        progress(0.02f);
    }

    TempFile audioTemp;
    const auto timestamp = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    audioTemp.path = fs::temp_directory_path() / ("resyne_video_export_" + timestamp + ".wav");

    TempFile stderrTemp;
    stderrTemp.path = fs::temp_directory_path() / ("resyne_video_export_stderr_" + timestamp + ".txt");

    if (!SequenceExporter::exportToWAV(audioTemp.path.string(), samples, metadata, [&](float p) {
            if (progress) {
                progress(0.02f + 0.13f * p);
            }
        })) {
        errorMessage = "Failed to reconstruct audio for video export";
        return false;
    }

    const double duration = computeDuration(samples, metadata);

    const std::string command = buildFFmpegCommand(options.ffmpegExecutable,
                                                   audioTemp.path,
                                                   outputPath,
                                                   stderrTemp.path,
                                                   width,
                                                   height,
                                                   fps);

    std::vector<RGB> gradientHistory;
    const bool success = renderVideo(command,
                                     stderrTemp.path,
                                     width,
                                     height,
                                     fps,
                                     samples,
                                     options,
                                     progress,
                                     duration,
                                     errorMessage,
                                     options.exportGradient ? &gradientHistory : nullptr);

    if (!success) {
        return false;
    }

    if (options.exportGradient && !gradientHistory.empty()) {
        const std::string gradientOutputPath = getGradientFilename(outputPath);

        TempFile gradientStderrTemp;
        gradientStderrTemp.path = fs::temp_directory_path() / ("resyne_gradient_export_stderr_" + timestamp + ".txt");

        const std::string gradientCommand = buildFFmpegCommand(options.ffmpegExecutable,
                                                               audioTemp.path,
                                                               gradientOutputPath,
                                                               gradientStderrTemp.path,
                                                               width,
                                                               height,
                                                               fps);

        const int totalFrames = static_cast<int>(gradientHistory.size());
        const int lineStride = width * 3;
        const size_t frameBytes = static_cast<size_t>(lineStride) * static_cast<size_t>(height);
        std::vector<uint8_t> frame(frameBytes, 0);

        FILE* pipe = openPipe(gradientCommand);
        if (!pipe) {
            errorMessage = "Unable to start FFmpeg for gradient export";
            return false;
        }

        constexpr size_t kPipeBufferSize = 4 * 1024 * 1024;
        std::vector<char> pipeBuffer(kPipeBufferSize);
        setvbuf(pipe, pipeBuffer.data(), _IOFBF, kPipeBufferSize);

        RGB backgroundColour = gradientHistory.empty() ? RGB{0.0f, 0.0f, 0.0f} : gradientHistory.front();

        for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex) {
            std::vector<RGB> currentHistory(gradientHistory.begin(),
                                          gradientHistory.begin() + frameIndex + 1);

            paintGradientFrame(frame, width, height, currentHistory, backgroundColour);

            if (fwrite(frame.data(), 1, frame.size(), pipe) != frame.size()) {
                errorMessage = "Failed to stream gradient frame to FFmpeg";
                closePipe(pipe);
                return false;
            }

            if (progress) {
                const float fraction = static_cast<float>(frameIndex + 1) / static_cast<float>(totalFrames);
                progress(0.5f + 0.4f * fraction);
            }
        }

        const int exitCode = closePipe(pipe);
        if (exitCode != 0) {
            errorMessage = "FFmpeg exited with an error during gradient export (code " + std::to_string(exitCode) + ")";
            return false;
        }
    }

    if (progress) {
        progress(1.0f);
    }

    return true;
}

}

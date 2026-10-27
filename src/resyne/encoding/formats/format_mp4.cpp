#include "resyne/encoding/formats/format_mp4.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "colour/colour_core.h"
#include "colour/colour_presentation.h"
#include "resyne/recorder/colour_cache_utils.h"
#include "ui/smoothing/smoothing.h"
#include "resyne/encoding/formats/rsyn_presentation.h"
#include "utilities/video/ffmpeg_locator.h"

namespace ReSyne::Encoding::Video {

namespace {
namespace fs = std::filesystem;

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

struct EncoderSettings {
    std::string name;
    std::string pixelFormat;
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
    FILE* pipe = _popen(command.c_str(), "wb");
    if (pipe) {
        _setmode(_fileno(pipe), _O_BINARY);
    }
    return pipe;
}
int closePipe(FILE* handle) {
    return _pclose(handle);
}
#else
void ignoreSigpipeForFFmpegPipes() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::signal(SIGPIPE, SIG_IGN);
    });
}

FILE* openPipe(const std::string& command) {
    ignoreSigpipeForFFmpegPipes();
    return popen(command.c_str(), "w");
}
int closePipe(FILE* handle) {
    return pclose(handle);
}
#endif

inline uint16_t toWord(float value) {
    return static_cast<uint16_t>(std::clamp(value, 0.0f, 1.0f) * 65535.0f + 0.5f);
}

class ColourTimelineSampler {
public:
    ColourTimelineSampler(const ColourPresentation::ColourTrack& track,
                          ColourCore::ColourSpace space, bool gamut)
        : track_(track), space_(space), gamut_(gamut) {}
    RGB colourAt(double timeSeconds) const {
        const auto rgb = track_.rgbAt(timeSeconds, space_, gamut_);
        return {rgb.r, rgb.g, rgb.b};
    }
private:
    const ColourPresentation::ColourTrack& track_;
    ColourCore::ColourSpace space_;
    bool gamut_;
};

std::shared_ptr<const ColourPresentation::ColourTrack> prepareColourTrack(
    const std::vector<AudioColourSample>& samples, const AudioMetadata& metadata, const ExportOptions& options,
    const std::function<void(float)>& progress) {
    RSYNPresentationSettings settings;
    settings.colourSpace = options.colourSpace;
    settings.applyGamutMapping = options.applyGamutMapping;
    settings.smoothingEnabled = options.smoothingEnabled;
    settings.manualSmoothing = options.manualSmoothing;
    settings.smoothingAmount = options.smoothingAmount;
    settings.lowGain = options.lowGain;
    settings.midGain = options.midGain;
    settings.highGain = options.highGain;
    if (const auto& stored = metadata.presentationData; stored && !stored->frames.empty() &&
        stored->settings.lowGain == settings.lowGain && stored->settings.midGain == settings.midGain &&
        stored->settings.highGain == settings.highGain) {
        const auto& previous = stored->settings;
        if (previous.smoothingEnabled == settings.smoothingEnabled && previous.manualSmoothing == settings.manualSmoothing &&
            (!settings.manualSmoothing ||
             previous.smoothingAmount == settings.smoothingAmount))
            return RSYNPresentation::colourTrack(*stored, settings.smoothingEnabled);
        {
            RSYNPresentationData rebuilt;
            rebuilt.settings = settings;
            RSYNPresentation::ReplayState replay(settings);
            rebuilt.frames.reserve(stored->frames.size());
            for (const auto& frame : stored->frames) {
                if (progress) progress(0.15f);
                rebuilt.frames.push_back(replay.processAnalysed(frame));
            }
            return RSYNPresentation::colourTrack(rebuilt, settings.smoothingEnabled);
        }
    }
    const auto presentation = RSYNPresentation::buildPresentationData(samples, settings,
        [&](float) { if (progress) progress(0.15f); });
    return RSYNPresentation::colourTrack(*presentation, settings.smoothingEnabled);
}

void paintColourFrame(std::vector<uint16_t>& buffer,
                      int width,
                      int height,
                      RGB colour) {
    ColourPresentation::applyOutputPrecision(colour.r, colour.g, colour.b);
    const uint16_t r = toWord(colour.r);
    const uint16_t g = toWord(colour.g);
    const uint16_t b = toWord(colour.b);
    const int lineStride = width * 3;

    for (int y = 0; y < height; ++y) {
        uint16_t* row = buffer.data() + static_cast<size_t>(y) * static_cast<size_t>(lineStride);
        for (int x = 0; x < width; ++x) {
            uint16_t* pixel = row + static_cast<size_t>(x) * 3;
            pixel[0] = r;
            pixel[1] = g;
            pixel[2] = b;
        }
    }
}

void paintGradientFrame(std::vector<uint16_t>& buffer, int width, int height,
                        const ColourPresentation::ColourTrack& track, double elapsed,
                        ColourCore::ColourSpace space, bool gamut) {
    const double step = std::max(0.0, elapsed) / width;
    for (int x = 0; x < width; ++x) {
        auto rgb = track.averageRGB(x * step, (x + 1) * step, space, gamut);
        ColourPresentation::applyOutputPrecision(rgb.r, rgb.g, rgb.b);
        const std::array<uint16_t, 3> pixel{toWord(rgb.r), toWord(rgb.g), toWord(rgb.b)};
        for (int y = 0; y < height; ++y) {
            const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3;
            std::copy(pixel.begin(), pixel.end(), buffer.data() + offset);
        }
    }
}

double computeDuration(const std::vector<AudioColourSample>& samples, const AudioMetadata& metadata) {
    return RSYNPresentation::durationSeconds(metadata, samples.empty() ? 0.0 : samples.back().timestamp);
}

std::string compatiblePixelFormatForEncoder(const std::string& encoder,
                                            const char* preferredPixelFormat) {
    if (encoder == "libx265") {
        return preferredPixelFormat && *preferredPixelFormat ? preferredPixelFormat : "yuv420p10le";
    }

    if (encoder == "libx264" ||
        encoder == "h264_videotoolbox" ||
        encoder == "hevc_videotoolbox" ||
        encoder == "mpeg4") {
        return "yuv420p";
    }

    return preferredPixelFormat && *preferredPixelFormat ? preferredPixelFormat : "yuv420p";
}

EncoderSettings makeEncoderSettings(const std::string& encoder,
                                    const char* preferredPixelFormat) {
    return EncoderSettings{
        encoder,
        compatiblePixelFormatForEncoder(encoder, preferredPixelFormat)
    };
}

EncoderSettings determineVideoEncoder(const ColourCore::ColourSpace colourSpace,
                                      const char* preferredPixelFormat) {
    if (const char* overrideCodec = std::getenv("RESYNE_FFMPEG_VIDEO_CODEC"); overrideCodec && *overrideCodec) {
        return makeEncoderSettings(overrideCodec, preferredPixelFormat);
    }

    const auto& locator = Utilities::Video::FFmpegLocator::instance();
    if (colourSpace == ColourCore::ColourSpace::Rec2020) {
        if (locator.supportsEncoder("libx265")) {
            return makeEncoderSettings("libx265", preferredPixelFormat);
        }
        if (locator.supportsEncoder("libx264")) {
            return makeEncoderSettings("libx264", preferredPixelFormat);
        }
        if (locator.supportsEncoder("mpeg4")) {
            return makeEncoderSettings("mpeg4", preferredPixelFormat);
        }
#if defined(__APPLE__)
        if (locator.supportsEncoder("hevc_videotoolbox")) {
            return makeEncoderSettings("hevc_videotoolbox", preferredPixelFormat);
        }
        if (locator.supportsEncoder("h264_videotoolbox")) {
            return makeEncoderSettings("h264_videotoolbox", preferredPixelFormat);
        }
#endif
    }

    if (locator.supportsEncoder("libx264")) {
        return makeEncoderSettings("libx264", preferredPixelFormat);
    }

    if (locator.supportsEncoder("mpeg4")) {
        return makeEncoderSettings("mpeg4", preferredPixelFormat);
    }

#if defined(__APPLE__)
    if (locator.supportsEncoder("h264_videotoolbox")) {
        return makeEncoderSettings("h264_videotoolbox", preferredPixelFormat);
    }
#endif

    return makeEncoderSettings("mpeg4", preferredPixelFormat);
}

void appendEncoderParameters(std::ostringstream& stream,
                             const std::string& encoder,
                             const std::string& pixelFormat) {
    if (encoder == "libx264") {
        stream << " -pix_fmt " << pixelFormat << " -preset medium -crf 18";
    } else if (encoder == "libx265") {
        stream << " -pix_fmt " << pixelFormat << " -preset medium -crf 18";
    } else if (encoder == "h264_videotoolbox") {
        stream << " -pix_fmt " << pixelFormat << " -b:v 12M -maxrate 12M -bufsize 24M -allow_sw 1";
    } else if (encoder == "hevc_videotoolbox") {
        stream << " -pix_fmt " << pixelFormat << " -b:v 20M -maxrate 20M -bufsize 40M -allow_sw 1";
    } else {
        stream << " -pix_fmt " << pixelFormat << " -q:v 3";
    }
}

std::string buildFFmpegCommand(const std::string& ffmpegPath,
                               const fs::path& audioPath,
                               const std::string& outputPath,
                               const fs::path& stderrPath,
                               int width,
                               int height,
                               int fps,
                               const ColourCore::ColourSpace colourSpace) {
    const auto& colourProfile = ColourCore::videoProfileFor(colourSpace);
    const EncoderSettings encoderSettings = determineVideoEncoder(colourSpace, colourProfile.pixelFormat);

    std::ostringstream oss;
    oss << '"' << ffmpegPath << '"'
        << " -y -loglevel error"
        << " -f rawvideo -pixel_format rgb48le"
        << " -video_size " << width << 'x' << height
        << " -framerate " << fps
        << " -i -"
        << " -f wav -i " << '"' << audioPath.string() << '"'
        << " -map 0:v:0 -map 1:a:0"
        << " -vf \"" << colourProfile.filter << ",format=" << encoderSettings.pixelFormat
        << ",setparams=range=limited:color_primaries=" << colourProfile.primaries
        << ":color_trc=" << colourProfile.transfer << ":colorspace=" << colourProfile.colourSpace << '"'
        << " -c:v " << encoderSettings.name;

    appendEncoderParameters(oss, encoderSettings.name, encoderSettings.pixelFormat);

    // https://ffmpeg.org/ffmpeg-formats.html#Format-Options
    oss << " -color_primaries " << colourProfile.primaries
        << " -color_trc " << colourProfile.transfer
        << " -colorspace " << colourProfile.colourSpace
        << " -color_range tv"
        << " -c:a aac -b:a 192k -movflags +faststart -avoid_negative_ts disabled"
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
                 const ColourPresentation::ColourTrack& track,
                 const ExportOptions& options,
                 const std::function<void(float)>& progress,
                 double duration,
                 std::string& errorMessage,
                 std::vector<RGB>* gradientHistory = nullptr) {
    const int totalFrames = std::max(1, static_cast<int>(std::ceil(duration * static_cast<double>(fps))));
    const int lineStride = width * 3;
    std::vector<uint16_t> frame(static_cast<size_t>(lineStride) * static_cast<size_t>(height), 0);

    ColourTimelineSampler sampler(track, options.colourSpace, options.applyGamutMapping);

    if (gradientHistory) {
        gradientHistory->reserve(static_cast<size_t>(totalFrames));
    }

    constexpr size_t kPipeBufferSize = 8 * 1024 * 1024;
    std::vector<char> pipeBuffer(kPipeBufferSize);
    std::unique_ptr<FILE, decltype(&closePipe)> pipe(openPipe(ffmpegCommand), closePipe);
    if (!pipe) {
        errorMessage = "Unable to start FFmpeg";
        return false;
    }

    setvbuf(pipe.get(), pipeBuffer.data(), _IOFBF, kPipeBufferSize);

    const float renderStart = 0.15f;
    const float renderSpan = gradientHistory ? 0.35f : 0.75f;

    for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex) {
        const double time = std::min(duration, static_cast<double>(frameIndex) / static_cast<double>(fps));
        const RGB colour = sampler.colourAt(time);

        if (gradientHistory) {
            gradientHistory->push_back(colour);
        }

        paintColourFrame(frame, width, height, colour);

        const size_t frameBytes = frame.size() * sizeof(uint16_t);
        const size_t bytesWritten = fwrite(frame.data(), 1, frameBytes, pipe.get());
        if (bytesWritten != frameBytes) {
            errorMessage = "Failed to stream video frame to FFmpeg";
            closePipe(pipe.release());
            return false;
        }

#ifdef _WIN32
        // Release buffered frames to the Windows pipe periodically during long exports.
        if (frameIndex % 5 == 0) {
            fflush(pipe.get());
        }
#endif

        if (progress) {
            const float fraction = static_cast<float>(frameIndex + 1) / static_cast<float>(totalFrames);
            progress(renderStart + renderSpan * fraction);
        }
    }

    fflush(pipe.get());

    const int exitCode = closePipe(pipe.release());
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
    const auto track = prepareColourTrack(samples, metadata, options, progress);

    const std::string command = buildFFmpegCommand(options.ffmpegExecutable,
                                                   audioTemp.path,
                                                   outputPath,
                                                   stderrTemp.path,
                                                   width,
                                                   height,
                                                   fps,
                                                   options.colourSpace);

    std::vector<RGB> gradientHistory;
    const bool success = renderVideo(command,
                                     stderrTemp.path,
                                     width,
                                     height,
                                     fps,
                                     *track,
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
                                                               fps,
                                                               options.colourSpace);

        const int totalFrames = static_cast<int>(gradientHistory.size());
        const int lineStride = width * 3;
        std::vector<uint16_t> frame(static_cast<size_t>(lineStride) * static_cast<size_t>(height), 0);

        constexpr size_t kPipeBufferSize = 8 * 1024 * 1024;
        std::vector<char> pipeBuffer(kPipeBufferSize);
        std::unique_ptr<FILE, decltype(&closePipe)> pipe(openPipe(gradientCommand), closePipe);
        if (!pipe) {
            errorMessage = "Unable to start FFmpeg for gradient export";
            return false;
        }

        setvbuf(pipe.get(), pipeBuffer.data(), _IOFBF, kPipeBufferSize);

        for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex) {
            paintGradientFrame(frame, width, height, *track,
                std::min(duration, static_cast<double>(frameIndex) / fps), options.colourSpace, options.applyGamutMapping);

            const size_t frameBytes = frame.size() * sizeof(uint16_t);
            if (fwrite(frame.data(), 1, frameBytes, pipe.get()) != frameBytes) {
                errorMessage = "Failed to stream gradient frame to FFmpeg";
                closePipe(pipe.release());
                return false;
            }

            if (progress) {
                const float fraction = static_cast<float>(frameIndex + 1) / static_cast<float>(totalFrames);
                progress(0.5f + 0.4f * fraction);
            }
        }

        const int exitCode = closePipe(pipe.release());
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

#include "utilities/process.h"
#include "utilities/cancellation.h"
#include "utilities/video/ffmpeg_locator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if defined(__linux__)
#include <limits.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Utilities::Video {

namespace {
namespace fs = std::filesystem;

#ifdef _WIN32
constexpr char PATH_SEPARATOR = ';';
#else
constexpr char PATH_SEPARATOR = ':';
#endif

std::string trim(const std::string& input) {
    const auto begin = std::find_if_not(input.begin(), input.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto end = std::find_if_not(input.rbegin(), input.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

bool isExecutable(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec)) {
        return false;
    }
#ifndef _WIN32
    const auto perms = fs::status(path, ec).permissions();
    if (ec) {
        return false;
    }
    const auto execMask = fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
    if ((perms & execMask) == fs::perms::none) {
        return false;
    }
#endif
    return true;
}

std::optional<fs::path> findInPath(const std::string& executableName) {
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) {
        return std::nullopt;
    }

    std::stringstream stream(pathEnv);
    std::string segment;
    while (std::getline(stream, segment, PATH_SEPARATOR)) {
        if (segment.empty()) {
            continue;
        }
        fs::path base = fs::path(segment);
        fs::path candidate = base / executableName;
        if (isExecutable(candidate)) {
            return candidate;
        }
#ifdef _WIN32
        if (!candidate.has_extension()) {
            fs::path exeCandidate = candidate;
            exeCandidate += ".exe";
            if (isExecutable(exeCandidate)) {
                return exeCandidate;
            }
        }
#endif
    }

    return std::nullopt;
}

bool queryVersion(const fs::path& executable, std::string& outVersion) {
    try {
        Utilities::Process process("\"" + executable.string() + "\" -version 2>&1", Utilities::Process::Mode::Read);
        const auto output = process.readAll();
        if (process.finish() != 0) return false;
        outVersion = trim(output.substr(0, output.find('\n')));
        return !outVersion.empty();
    } catch (const Utilities::OperationCancelled&) { throw; }
    catch (const std::exception&) { return false; }
}

bool queryEncoders(const fs::path& executable, std::vector<std::string>& encoders) {
    try {
        Utilities::Process process("\"" + executable.string() + "\" -hide_banner -encoders 2>&1", Utilities::Process::Mode::Read);
        std::istringstream output(process.readAll());
        if (process.finish() != 0) return false;
        std::string line;
        while (std::getline(output, line)) {
            std::istringstream fields(trim(line));
            std::string descriptor, name;
            fields >> descriptor >> name;
            if (descriptor.size() == 6 && descriptor[0] != '=' && !name.empty()) encoders.push_back(std::move(name));
        }
        return !encoders.empty();
    } catch (const Utilities::OperationCancelled&) { throw; }
    catch (const std::exception&) { return false; }
}

std::vector<std::string> defaultExecutableNames() {
    std::vector<std::string> names = {"ffmpeg"};
#ifdef _WIN32
    names.emplace_back("ffmpeg.exe");
#endif
    return names;
}

std::optional<fs::path> executableDirectory() {
#if defined(_WIN32)
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size()) {
        return std::nullopt;
    }
    return fs::path(buffer.data()).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return std::nullopt;
    }
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(fs::path(buffer.c_str()), ec);
    if (ec) {
        resolved = fs::path(buffer.c_str());
    }
    return resolved.parent_path();
#elif defined(__linux__)
    std::array<char, PATH_MAX> buffer{};
    const ssize_t bytes = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (bytes <= 0) {
        return std::nullopt;
    }
    buffer[static_cast<size_t>(bytes)] = '\0';
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(fs::path(buffer.data()), ec);
    if (ec) {
        resolved = fs::path(buffer.data());
    }
    return resolved.parent_path();
#else
    return std::nullopt;
#endif
}

std::vector<fs::path> bundledCandidates() {
    std::vector<fs::path> paths;
    if (const char* overridePath = std::getenv("RESYNE_BUNDLED_FFMPEG")) {
        if (*overridePath) {
            paths.emplace_back(overridePath);
        }
    }

    if (auto executableDir = executableDirectory()) {
        paths.emplace_back(*executableDir / "assets/bin/ffmpeg");
#if defined(_WIN32)
        paths.emplace_back(*executableDir / "assets/bin/ffmpeg.exe");
        paths.emplace_back(*executableDir / "ffmpeg.exe");
#else
        paths.emplace_back(*executableDir / "ffmpeg");
#endif
#if defined(__APPLE__)
        fs::path contentsPath = executableDir->parent_path();
        fs::path bundleResources = contentsPath / "Resources";
        paths.emplace_back(bundleResources / "bin/ffmpeg");
#endif
    }

    paths.emplace_back("assets/bin/ffmpeg");
#if defined(_WIN32)
    paths.emplace_back("assets/bin/ffmpeg.exe");
#endif

    return paths;
}

}


FFmpegLocator& FFmpegLocator::instance() {
    static FFmpegLocator locator;
    return locator;
}

void FFmpegLocator::detect() {
#if defined(_WIN32)
    attempted_ = true;
    available_ = false;
    executablePath_.clear();
    versionString_.clear();
    encoderNames_.clear();
    return;
#else
    attempted_ = true;
    available_ = false;
    executablePath_.clear();
    versionString_.clear();
    encoderNames_.clear();

    std::vector<fs::path> candidates;

    const auto bundled = bundledCandidates();
    candidates.insert(candidates.end(), bundled.begin(), bundled.end());

    const char* overrideVars[] = {"RESYNE_FFMPEG_PATH", "FFMPEG_PATH"};
    for (const char* var : overrideVars) {
        if (const char* value = std::getenv(var); value && *value) {
            candidates.emplace_back(value);
        }
    }

    const auto names = defaultExecutableNames();
    candidates.reserve(candidates.size() + names.size());
    for (const auto& name : names) {
        candidates.emplace_back(name);
    }

    for (const auto& rawCandidate : candidates) {
        fs::path resolved = rawCandidate;
        if (!resolved.is_absolute()) {
            if (auto located = findInPath(rawCandidate.string())) {
                resolved = *located;
            } else {
                continue;
            }
        }

        if (!isExecutable(resolved)) {
            continue;
        }

        std::string version;
        if (queryVersion(resolved, version)) {
            available_ = true;
            executablePath_ = resolved.string();
            versionString_ = version;
            encoderNames_.clear();
            queryEncoders(resolved, encoderNames_);
            return;
        }
    }
#endif
}

FFmpegLocator::~FFmpegLocator() {
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
}

void FFmpegLocator::refresh() {
    FFmpegLocator candidate;
    candidate.detect();
    auto status = std::make_shared<Status>();
    status->attempted = candidate.attempted_;
    status->available = candidate.available_;
    status->executable = std::move(candidate.executablePath_);
    status->version = std::move(candidate.versionString_);
    status->encoders = std::move(candidate.encoderNames_);
    std::atomic_store(&status_, std::shared_ptr<const Status>(std::move(status)));
}

void FFmpegLocator::refreshAsync() {
    if (worker_.joinable()) { worker_.request_stop(); worker_.join(); }
    worker_ = std::jthread([this](std::stop_token stop) {
        Utilities::CancellationScope scope(stop);
        try { refresh(); } catch (const Utilities::OperationCancelled&) {}
        catch (const std::exception&) {
            auto status = std::make_shared<Status>();
            status->attempted = true;
            std::atomic_store(&status_, std::shared_ptr<const Status>(std::move(status)));
        }
    });
}

bool FFmpegLocator::supportsEncoder(std::string_view name) const {
    const auto status = std::atomic_load(&status_);
    return std::any_of(status->encoders.begin(), status->encoders.end(), [&](const std::string& candidate) {
        return candidate == name;
    });
}

}

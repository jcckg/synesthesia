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
#include <system_error>
#include <vector>

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

#ifdef _WIN32
using PipeHandle = std::unique_ptr<FILE, decltype(&_pclose)>;
PipeHandle makePipe(const std::string& command) {
    return PipeHandle(_popen(command.c_str(), "r"), _pclose);
}
#else
using PipeHandle = std::unique_ptr<FILE, decltype(&pclose)>;
PipeHandle makePipe(const std::string& command) {
    return PipeHandle(popen(command.c_str(), "r"), pclose);
}
#endif

bool queryVersion(const fs::path& executable, std::string& outVersion) {
    std::string command = "\"" + executable.string() + "\" -version";
#ifndef _WIN32
    command += " 2>&1";
#else
    command += " 2>&1";
#endif

    auto pipe = makePipe(command);
    if (!pipe) {
        return false;
    }

    std::array<char, 256> buffer{};
    if (!fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get())) {
        return false;
    }

    outVersion = trim(buffer.data());
    return !outVersion.empty();
}

std::vector<std::string> defaultExecutableNames() {
    std::vector<std::string> names = {"ffmpeg"};
#ifdef _WIN32
    names.emplace_back("ffmpeg.exe");
#endif
    return names;
}

}


FFmpegLocator& FFmpegLocator::instance() {
    static FFmpegLocator locator;
    return locator;
}

void FFmpegLocator::refresh() {
    attempted_ = true;
    available_ = false;
    executablePath_.clear();
    versionString_.clear();

    std::vector<fs::path> candidates;
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
            return;
        }
    }
}

}

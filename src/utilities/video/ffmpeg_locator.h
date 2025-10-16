#pragma once

#include <string>

namespace Utilities::Video {

class FFmpegLocator {
public:
    static FFmpegLocator& instance();

    void refresh();

    [[nodiscard]] bool isAvailable() const { return available_; }
    [[nodiscard]] bool hasAttemptedDetection() const { return attempted_; }
    [[nodiscard]] const std::string& executablePath() const { return executablePath_; }
    [[nodiscard]] const std::string& version() const { return versionString_; }

private:
    FFmpegLocator() = default;

    bool attempted_ = false;
    bool available_ = false;
    std::string executablePath_;
    std::string versionString_;
};

}

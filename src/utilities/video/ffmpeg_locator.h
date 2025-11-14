#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Utilities::Video {

class FFmpegLocator {
public:
    static FFmpegLocator& instance();

    void refresh();

    [[nodiscard]] bool isAvailable() const { return available_; }
    [[nodiscard]] bool hasAttemptedDetection() const { return attempted_; }
    [[nodiscard]] const std::string& executablePath() const { return executablePath_; }
    [[nodiscard]] const std::string& version() const { return versionString_; }
    [[nodiscard]] bool supportsEncoder(std::string_view name) const;

private:
    FFmpegLocator() = default;

    bool attempted_ = false;
    bool available_ = false;
    std::string executablePath_;
    std::string versionString_;
    std::vector<std::string> encoderNames_;
};

}

#pragma once

#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <string_view>
#include <vector>

namespace Utilities::Video {

class FFmpegLocator {
public:
    static FFmpegLocator& instance();

    ~FFmpegLocator();
    void refresh();
    void refreshAsync();

    [[nodiscard]] bool isAvailable() const { return std::atomic_load(&status_)->available; }
    [[nodiscard]] bool hasAttemptedDetection() const { return std::atomic_load(&status_)->attempted; }
    [[nodiscard]] std::string executablePath() const { return std::atomic_load(&status_)->executable; }
    [[nodiscard]] std::string version() const { return std::atomic_load(&status_)->version; }
    [[nodiscard]] bool supportsEncoder(std::string_view name) const;

private:
    FFmpegLocator() = default;

    struct Status {
        bool attempted = false, available = false;
        std::string executable, version;
        std::vector<std::string> encoders;
    };
    std::shared_ptr<const Status> status_ = std::make_shared<const Status>();
    std::jthread worker_;
    void detect();
    bool attempted_ = false;
    bool available_ = false;
    std::string executablePath_;
    std::string versionString_;
    std::vector<std::string> encoderNames_;
};

}

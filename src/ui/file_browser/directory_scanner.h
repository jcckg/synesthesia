#pragma once

#include "ui/file_browser/file_browser.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace UI::FileBrowser {

class DirectoryScanner {
public:
    struct Result {
        std::vector<Entry> files;
        std::string error;
        bool truncated = false;
    };

    DirectoryScanner();
    ~DirectoryScanner();
    void request(std::string directory);
    std::optional<Result> takeResult();

private:
    void run(std::stop_token stop);
    Result scan(const std::string& directory, uint64_t generation, std::stop_token stop) const;

    std::atomic<uint64_t> generation_{0};
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<std::string> pendingDirectory_;
    std::optional<Result> result_;
    std::jthread worker_;
};

}

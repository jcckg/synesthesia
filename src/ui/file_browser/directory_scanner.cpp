#include "ui/file_browser/directory_scanner.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "resyne/recorder/recorder.h"

namespace UI::FileBrowser {

namespace {

struct ScanCancelled {};

std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

}

DirectoryScanner::DirectoryScanner() : worker_([this](std::stop_token stop) { run(stop); }) {}

DirectoryScanner::~DirectoryScanner() {
    worker_.request_stop();
    wake_.notify_all();
}

void DirectoryScanner::request(std::string directory) {
    {
        std::lock_guard lock(mutex_);
        ++generation_;
        pendingDirectory_ = std::move(directory);
        result_.reset();
    }
    wake_.notify_all();
}

std::optional<DirectoryScanner::Result> DirectoryScanner::takeResult() {
    std::lock_guard lock(mutex_);
    auto result = std::move(result_);
    result_.reset();
    return result;
}

void DirectoryScanner::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        std::string directory;
        uint64_t generation;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stop, [this] { return pendingDirectory_.has_value(); });
            if (stop.stop_requested()) return;
            directory = std::move(*pendingDirectory_);
            pendingDirectory_.reset();
            generation = generation_.load();
        }
        Result result;
        try {
            result = scan(directory, generation, stop);
        } catch (const ScanCancelled&) {
            continue;
        } catch (const std::exception& error) {
            result.error = "Unable to read directory: " + std::string(error.what());
        }
        std::lock_guard lock(mutex_);
        if (!stop.stop_requested() && generation == generation_.load()) result_ = std::move(result);
    }
}

DirectoryScanner::Result DirectoryScanner::scan(const std::string& directory, uint64_t generation,
                                                std::stop_token stop) const {
    const auto checkCancellation = [&] {
        if (stop.stop_requested() || generation != generation_.load(std::memory_order_relaxed)) throw ScanCancelled{};
    };
    Result result;
    std::error_code error;
    std::filesystem::directory_iterator iterator(
        std::filesystem::path(std::u8string(directory.begin(), directory.end())), error);
    if (error) {
        result.error = "Unable to read directory: " + error.message();
        return result;
    }
    size_t entryBytes = 0;
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        checkCancellation();
        const auto& item = *iterator;
        std::error_code entryError;
        const auto status = item.symlink_status(entryError);
        const bool isDirectory = !entryError && std::filesystem::is_directory(status);
        const std::string name = utf8(item.path().filename());
        const std::string path = utf8(item.path());
        if (name != ".DS_Store" && !name.starts_with("._") &&
            (isDirectory || (ReSyne::Recorder::isSupportedImportFile(path) && item.is_regular_file(entryError)))) {
            entryBytes += sizeof(Entry) + path.size() + name.size() * 2;
            if (result.files.size() == maximumEntries || entryBytes > 64u * 1024u * 1024u) {
                result.truncated = true;
                break;
            }
            auto key = name;
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            result.files.push_back({path, name, std::move(key), isDirectory});
        }
        iterator.increment(error);
        if (error) {
            result.error = "Unable to finish reading directory: " + error.message();
            break;
        }
    }
    size_t comparisons = 0;
    std::sort(result.files.begin(), result.files.end(), [&](const Entry& a, const Entry& b) {
        if (++comparisons % 1024 == 0) checkCancellation();
        if (a.directory != b.directory) return a.directory;
        return a.sortKey == b.sortKey ? a.name < b.name : a.sortKey < b.sortKey;
    });
    checkCancellation();
    return result;
}

}

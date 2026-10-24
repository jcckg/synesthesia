#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace ReSyne {

class ExportDestination {
public:
    explicit ExportDestination(const std::filesystem::path& destination, bool gradient) {
        static std::atomic<unsigned long long> serial{0};
        const auto parent = destination.has_parent_path() ? destination.parent_path() : std::filesystem::path(".");
        do {
            directory_ = parent / (".resyne-export-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++serial));
        } while (!std::filesystem::create_directory(directory_));
        const auto output = directory_ / "output";
        std::filesystem::create_directory(output);
        files_.push_back({output / destination.filename(), destination, directory_ / "original-0"});
        if (gradient) {
            const auto name = destination.stem().string() + "_gradient" + destination.extension().string();
            files_.push_back({output / name, parent / name, directory_ / "original-1"});
        }
    }

    ~ExportDestination() {
        if (!preserve_) {
            std::error_code error;
            std::filesystem::remove_all(directory_, error);
        }
    }

    ExportDestination(const ExportDestination&) = delete;
    ExportDestination& operator=(const ExportDestination&) = delete;

    std::string path() const { return files_.front().staged.string(); }

    void commit() {
        for (const auto& file : files_) {
            if (!std::filesystem::is_regular_file(file.staged))
                throw std::runtime_error("Export did not produce the expected file");
            if (std::filesystem::exists(file.destination) && !std::filesystem::is_regular_file(file.destination))
                throw std::runtime_error("Export destination is not a regular file");
        }
        try {
            for (auto& file : files_) {
                if (std::filesystem::exists(file.destination)) {
                    std::filesystem::rename(file.destination, file.backup);
                    file.backedUp = true;
                }
                std::filesystem::rename(file.staged, file.destination);
                file.installed = true;
            }
        } catch (...) {
            for (auto it = files_.rbegin(); it != files_.rend(); ++it) {
                std::error_code error;
                if (it->installed) {
                    std::filesystem::rename(it->destination, it->staged, error);
                    if (error) preserve_ = true;
                }
                if (it->backedUp) {
                    error.clear();
                    std::filesystem::rename(it->backup, it->destination, error);
                    if (error) preserve_ = true;
                }
            }
            if (preserve_)
                throw std::runtime_error("Unable to restore the previous export; retained files in " + directory_.string());
            throw;
        }
    }

private:
    struct File {
        std::filesystem::path staged, destination, backup;
        bool backedUp = false;
        bool installed = false;
    };
    std::filesystem::path directory_;
    std::vector<File> files_;
    bool preserve_ = false;
};

}

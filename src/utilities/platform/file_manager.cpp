#include "utilities/platform/file_manager.h"

#include <filesystem>
#include <exception>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#elif !defined(__APPLE__)
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;
#endif

namespace Utilities::FileManager {

Result revealNative(const std::filesystem::path& path);

Result reveal(const std::string& value) {
    if (value.empty() || value.find('\0') != std::string::npos) return {false, "The selected item path is invalid."};
    try {
        std::error_code error;
        const auto path = std::filesystem::absolute(std::filesystem::path(std::u8string(value.begin(), value.end())), error);
        if (error || !std::filesystem::exists(path, error) || error) return {false, "The selected item is no longer available."};
        return revealNative(path);
    } catch (const std::exception&) {
        return {false, "Unable to reveal the selected item."};
    }
}

#ifdef _WIN32

std::string name() {
    return "File Explorer";
}

Result revealNative(const std::filesystem::path& path) {
    const HRESULT initialised = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialised) && initialised != RPC_E_CHANGED_MODE) return {false, "Unable to initialise File Explorer."};
    PIDLIST_ABSOLUTE item = nullptr;
    HRESULT result = SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr);
    if (SUCCEEDED(result)) result = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    CoTaskMemFree(item);
    if (SUCCEEDED(initialised)) CoUninitialize();
    return SUCCEEDED(result) ? Result{true, {}} : Result{false, "Unable to reveal the selected item in File Explorer."};
}

#elif !defined(__APPLE__)

namespace {

struct ProcessResult {
    bool success = false;
    std::string output;
};

ProcessResult run(std::vector<std::string> arguments) {
    int descriptors[2];
    if (pipe(descriptors) != 0) return {};
    fcntl(descriptors[0], F_SETFD, FD_CLOEXEC);
    fcntl(descriptors[1], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&actions, descriptors[0]);
    posix_spawn_file_actions_addclose(&actions, descriptors[1]);
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    pid_t process = 0;
    const int launched = posix_spawnp(&process, argv.front(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(descriptors[1]);
    if (launched != 0) { close(descriptors[0]); return {}; }
    fcntl(descriptors[0], F_SETFL, O_NONBLOCK);
    ProcessResult result;
    std::array<char, 1024> buffer;
    const auto readOutput = [&] {
        for (;;) {
            const auto count = read(descriptors[0], buffer.data(), buffer.size());
            if (count <= 0) break;
            if (result.output.size() < 16384) result.output.append(buffer.data(), static_cast<std::size_t>(count));
        }
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    for (;;) {
        readOutput();
        int status = 0;
        const auto waited = waitpid(process, &status, WNOHANG);
        if (waited == process) {
            result.success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            break;
        }
        if (waited < 0 && errno != EINTR) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(process, SIGKILL);
            while (waitpid(process, &status, 0) < 0 && errno == EINTR) {}
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    readOutput();
    close(descriptors[0]);
    return result;
}

std::string fileUri(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    constexpr char digits[] = "0123456789ABCDEF";
    std::string uri = "file://";
    for (const auto byte : bytes) {
        const auto value = static_cast<unsigned char>(byte);
        if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '/' || value == '-' || value == '_' || value == '.' || value == '~') {
            uri += static_cast<char>(value);
        } else {
            uri += '%';
            uri += digits[value >> 4];
            uri += digits[value & 15];
        }
    }
    return uri;
}

}

std::string name() {
    auto desktop = run({"xdg-mime", "query", "default", "inode/directory"});
    if (!desktop.success) return "File Manager";
    const auto end = desktop.output.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) return "File Manager";
    desktop.output.resize(end + 1);
    if (desktop.output.size() > 256 || !desktop.output.ends_with(".desktop") ||
        desktop.output.find('/') != std::string::npos || desktop.output.find('\0') != std::string::npos) return "File Manager";
    std::vector<std::filesystem::path> directories;
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) directories.emplace_back(data);
    else if (const char* home = std::getenv("HOME")) directories.emplace_back(std::filesystem::path(home) / ".local/share");
    const char* data = std::getenv("XDG_DATA_DIRS");
    std::stringstream paths(data && *data ? data : "/usr/local/share:/usr/share");
    std::string directory;
    while (std::getline(paths, directory, ':')) if (!directory.empty()) directories.emplace_back(directory);
    for (const auto& base : directories) {
        std::ifstream file(base / "applications" / desktop.output);
        std::string line;
        bool entry = false;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with('[')) entry = line == "[Desktop Entry]";
            if (entry && line.starts_with("Name=") && line.size() > 5) return line.substr(5);
        }
    }
    return "File Manager";
}

Result revealNative(const std::filesystem::path& path) {
    const auto result = run({"gdbus", "call", "--session", "--dest", "org.freedesktop.FileManager1",
        "--object-path", "/org/freedesktop/FileManager1", "--method", "org.freedesktop.FileManager1.ShowItems",
        "--timeout", "3", "['" + fileUri(path) + "']", ""});
    return result.success ? Result{true, {}} : Result{false, "Unable to reveal the selected item in the file manager."};
}

#endif

}

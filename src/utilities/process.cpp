#include "utilities/process.h"
#include "utilities/cancellation.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <limits>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace Utilities {
namespace {
using Clock = std::chrono::steady_clock;
void check(Clock::time_point deadline) {
    checkCancellation();
    if (Clock::now() >= deadline) throw std::runtime_error("External process timed out");
}
}

struct Process::Impl {
    Mode mode;
    bool finished = false;
#ifdef _WIN32
    HANDLE pipe = INVALID_HANDLE_VALUE, process = nullptr, job = nullptr;
    explicit Impl(const std::string& command, Mode selected) : mode(selected) {
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE child = INVALID_HANDLE_VALUE;
        if (mode == Mode::Read) {
            if (!CreatePipe(&pipe, &child, &security, 65536)) throw std::runtime_error("Unable to create process pipe");
            SetHandleInformation(pipe, HANDLE_FLAG_INHERIT, 0);
        } else {
            const auto name = L"\\\\.\\pipe\\synesthesia-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(reinterpret_cast<std::uintptr_t>(this));
            pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) throw std::runtime_error("Unable to create process pipe");
            child = CreateFileW(name.c_str(), GENERIC_READ, 0, &security, OPEN_EXISTING, 0, nullptr);
            if (child == INVALID_HANDLE_VALUE) { CloseHandle(pipe); throw std::runtime_error("Unable to connect process pipe"); }
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = mode == Mode::Write ? child : GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = mode == Mode::Read ? child : GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = mode == Mode::Read ? child : GetStdHandle(STD_ERROR_HANDLE);
        const auto shell = std::string("cmd.exe /d /s /c \"") + command + "\"";
        const int length = MultiByteToWideChar(CP_UTF8, 0, shell.data(), static_cast<int>(shell.size()), nullptr, 0);
        std::vector<wchar_t> wide(static_cast<std::size_t>(length) + 1);
        MultiByteToWideChar(CP_UTF8, 0, shell.data(), static_cast<int>(shell.size()), wide.data(), length);
        PROCESS_INFORMATION info{};
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const bool configured = job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        const bool started = configured && CreateProcessW(nullptr, wide.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &info);
        CloseHandle(child);
        if (!started) {
            CloseHandle(pipe);
            if (job) CloseHandle(job);
            throw std::runtime_error("Unable to start external process");
        }
        process = info.hProcess;
        if (!AssignProcessToJobObject(job, process)) {
            TerminateProcess(process, 1);
            CloseHandle(info.hThread); CloseHandle(process); CloseHandle(pipe); CloseHandle(job);
            throw std::runtime_error("Unable to supervise external process");
        }
        ResumeThread(info.hThread);
        CloseHandle(info.hThread);
    }
    ~Impl() {
        if (pipe != INVALID_HANDLE_VALUE) { CancelIoEx(pipe, nullptr); CloseHandle(pipe); }
        if (!finished && job) TerminateJobObject(job, 1);
        if (process) { WaitForSingleObject(process, 1000); CloseHandle(process); }
        if (job) CloseHandle(job);
    }
#else
    int pipe = -1;
    pid_t process = -1;
    explicit Impl(const std::string& command, Mode selected) : mode(selected) {
        static std::once_flag signals;
        std::call_once(signals, [] { std::signal(SIGPIPE, SIG_IGN); });
        int ends[2];
        if (::pipe(ends) != 0) throw std::runtime_error("Unable to create process pipe");
        for (const int descriptor : ends) fcntl(descriptor, F_SETFD, FD_CLOEXEC);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        const int parent = mode == Mode::Read ? ends[0] : ends[1];
        const int child = mode == Mode::Read ? ends[1] : ends[0];
        posix_spawn_file_actions_adddup2(&actions, child, mode == Mode::Read ? STDOUT_FILENO : STDIN_FILENO);
        if (mode == Mode::Read) posix_spawn_file_actions_adddup2(&actions, child, STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, parent);
        posix_spawn_file_actions_addclose(&actions, child);
        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attributes, 0);
        std::array<char*, 4> arguments{const_cast<char*>("sh"), const_cast<char*>("-c"), const_cast<char*>(command.c_str()), nullptr};
        const int error = posix_spawnp(&process, "sh", &actions, &attributes, arguments.data(), environ);
        posix_spawnattr_destroy(&attributes);
        posix_spawn_file_actions_destroy(&actions);
        close(child);
        if (error != 0) { close(parent); process = -1; throw std::runtime_error("Unable to start external process"); }
        pipe = parent;
        fcntl(pipe, F_SETFL, fcntl(pipe, F_GETFL) | O_NONBLOCK);
#ifdef __APPLE__
        if (mode == Mode::Write) fcntl(pipe, F_SETNOSIGPIPE, 1);
#endif
    }
    ~Impl() {
        if (pipe >= 0) close(pipe);
        if (process > 0 && !finished) {
            kill(-process, SIGKILL);
            int status;
            while (waitpid(process, &status, 0) < 0 && errno == EINTR) {}
        }
    }
#endif
};

Process::Process(const std::string& command, Mode mode) : impl_(std::make_unique<Impl>(command, mode)) {}
Process::~Process() = default;

std::string Process::readAll() {
    std::string output;
    std::array<char, 4096> buffer;
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    for (;;) {
        check(deadline);
#ifdef _WIN32
        DWORD available = 0, count = 0;
        if (!PeekNamedPipe(impl_->pipe, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available == 0) {
            if (WaitForSingleObject(impl_->process, 0) == WAIT_OBJECT_0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (!ReadFile(impl_->pipe, buffer.data(), std::min<DWORD>(available, static_cast<DWORD>(buffer.size())), &count, nullptr)) break;
#else
        pollfd descriptor{impl_->pipe, POLLIN, 0};
        const int ready = poll(&descriptor, 1, 20);
        if (ready < 0 && errno != EINTR) throw std::runtime_error("Unable to read process output");
        if (ready <= 0) continue;
        const auto count = read(impl_->pipe, buffer.data(), buffer.size());
        if (count == 0) break;
        if (count < 0) { if (errno == EAGAIN || errno == EINTR) continue; break; }
#endif
        output.append(buffer.data(), static_cast<std::size_t>(count));
        if (output.size() > 4 * 1024 * 1024) throw std::runtime_error("External process output exceeds the limit");
    }
    return output;
}

std::size_t Process::write(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t offset = 0;
    auto deadline = Clock::now() + std::chrono::seconds(30);
    while (offset < size) {
        check(deadline);
#ifdef _WIN32
        OVERLAPPED pending{};
        pending.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!pending.hEvent) throw std::runtime_error("Unable to monitor process input");
        DWORD count = 0;
        const bool complete = WriteFile(impl_->pipe, bytes + offset, static_cast<DWORD>(std::min<std::size_t>(size - offset, 65536)), &count, &pending);
        if (!complete && GetLastError() == ERROR_IO_PENDING) {
            try {
                while (WaitForSingleObject(pending.hEvent, 20) == WAIT_TIMEOUT) check(deadline);
                if (!GetOverlappedResult(impl_->pipe, &pending, &count, FALSE)) count = 0;
            } catch (...) {
                CancelIoEx(impl_->pipe, &pending);
                GetOverlappedResult(impl_->pipe, &pending, &count, TRUE);
                CloseHandle(pending.hEvent);
                throw;
            }
        } else if (!complete) count = 0;
        CloseHandle(pending.hEvent);
        if (count == 0) break;
#else
        pollfd descriptor{impl_->pipe, POLLOUT, 0};
        const int ready = poll(&descriptor, 1, 20);
        if (ready < 0 && errno != EINTR) throw std::runtime_error("Unable to write process input");
        if (ready <= 0) continue;
        const auto count = ::write(impl_->pipe, bytes + offset, std::min<std::size_t>(size - offset, 65536));
        if (count < 0) { if (errno == EAGAIN || errno == EINTR) continue; break; }
        if (count == 0) break;
#endif
        offset += static_cast<std::size_t>(count);
        deadline = Clock::now() + std::chrono::seconds(30);
    }
    return offset;
}

int Process::finish() {
#ifdef _WIN32
    if (impl_->pipe != INVALID_HANDLE_VALUE) { CloseHandle(impl_->pipe); impl_->pipe = INVALID_HANDLE_VALUE; }
#else
    if (impl_->pipe >= 0) { close(impl_->pipe); impl_->pipe = -1; }
#endif
    const auto deadline = Clock::now() + std::chrono::seconds(impl_->mode == Mode::Read ? 10 : 60);
    for (;;) {
        check(deadline);
#ifdef _WIN32
        if (WaitForSingleObject(impl_->process, 20) != WAIT_OBJECT_0) continue;
        DWORD code = 1;
        GetExitCodeProcess(impl_->process, &code);
        impl_->finished = true;
        return static_cast<int>(code);
#else
        int status = 0;
        const auto result = waitpid(impl_->process, &status, WNOHANG);
        if (result == impl_->process) {
            impl_->finished = true;
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
        if (result < 0 && errno != EINTR) return 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
    }
}

}

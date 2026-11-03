#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <cstddef>

namespace Utilities {

class Process {
public:
    enum class Mode { Read, Write };
    Process(const std::string& command, Mode mode);
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    std::string readAll();
    std::size_t write(const void* data, std::size_t size);
    int finish();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

#pragma once

#include <string>

namespace Utilities::FileManager {

struct Result {
    bool success = false;
    std::string error;
};

std::string name();
Result reveal(const std::string& path);

}

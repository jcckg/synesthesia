#pragma once

#include <exception>
#include <stop_token>

namespace Utilities {

class OperationCancelled final : public std::exception {
public:
    const char* what() const noexcept override { return "Operation cancelled"; }
};

inline void checkCancellation(std::stop_token token) {
    if (token.stop_requested()) throw OperationCancelled{};
}

}

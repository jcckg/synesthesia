#pragma once

#include <exception>
#include <stop_token>

namespace Utilities {

class OperationCancelled final : public std::exception {
public:
    const char* what() const noexcept override { return "Operation cancelled"; }
};

inline thread_local std::stop_token operationCancellation;

class CancellationScope {
public:
    explicit CancellationScope(std::stop_token token) : previous_(operationCancellation) { operationCancellation = token; }
    ~CancellationScope() { operationCancellation = previous_; }
private:
    std::stop_token previous_;
};

inline void checkCancellation(std::stop_token token = operationCancellation) {
    if (token.stop_requested()) throw OperationCancelled{};
}

}

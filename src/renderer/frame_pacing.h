#pragma once

#include <algorithm>
#include <chrono>

namespace Renderer {

class FramePacing {
public:
    using Clock = std::chrono::steady_clock;

    FramePacing(Clock::time_point start, Clock::duration interval)
        : interval_(std::max(Clock::duration{1}, interval)), deadline_(start + interval_) {}

    Clock::time_point finish(Clock::time_point now) {
        const auto waitUntil = std::max(now, deadline_);
        deadline_ += interval_;
        if (deadline_ <= now) deadline_ = now + interval_;
        return waitUntil;
    }

private:
    Clock::duration interval_;
    Clock::time_point deadline_;
};

}

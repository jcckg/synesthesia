#pragma once

#include <functional>
#include <memory>

namespace Renderer {

class EventTrackingFrames {
public:
    EventTrackingFrames(std::function<void()> frame, double framesPerSecond);
    ~EventTrackingFrames();

    EventTrackingFrames(const EventTrackingFrames&) = delete;
    EventTrackingFrames& operator=(const EventTrackingFrames&) = delete;

    void rethrowPendingException();
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

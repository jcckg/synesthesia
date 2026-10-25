#include "renderer/event_tracking_frames.h"

#ifndef __APPLE__

namespace Renderer {

struct EventTrackingFrames::Impl {};

EventTrackingFrames::EventTrackingFrames(std::function<void()>, double) {}
EventTrackingFrames::~EventTrackingFrames() = default;

void EventTrackingFrames::rethrowPendingException() {}
void EventTrackingFrames::shutdown() {}

}

#endif

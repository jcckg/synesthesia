#include "renderer/event_tracking_frames.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <exception>
#include <utility>

@interface SynesthesiaTrackingFrames : NSObject {
@public
    std::function<void()> frame;
    std::exception_ptr failure;
    double interval;
    NSTimer* timer;
    NSMutableSet<NSMenu*>* menus;
    bool rendering;
}
- (void)menuBeganTracking:(NSNotification*)notification;
- (void)menuEndedTracking:(NSNotification*)notification;
- (void)renderFrame:(NSTimer*)sender;
- (void)stopTimer;
@end

@implementation SynesthesiaTrackingFrames

- (void)menuBeganTracking:(NSNotification*)notification {
    if (failure) return;
    [menus addObject:notification.object];
    if (timer != nil) return;
    timer = [NSTimer timerWithTimeInterval:interval target:self selector:@selector(renderFrame:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
}

- (void)menuEndedTracking:(NSNotification*)notification {
    [menus removeObject:notification.object];
    if (menus.count == 0) [self stopTimer];
}

- (void)renderFrame:(NSTimer*)sender {
    (void)sender;
    if (rendering || failure || !frame || menus.count == 0) return;
    rendering = true;
    @autoreleasepool {
        try {
            frame();
        } catch (...) {
            failure = std::current_exception();
            for (NSMenu* menu in [menus allObjects]) [menu cancelTracking];
            [self stopTimer];
        }
    }
    rendering = false;
}

- (void)stopTimer {
    [timer invalidate];
    timer = nil;
}

@end

namespace Renderer {

struct EventTrackingFrames::Impl {
    SynesthesiaTrackingFrames* target;

    Impl(std::function<void()> callback, double framesPerSecond)
        : target([[SynesthesiaTrackingFrames alloc] init]) {
        target->frame = std::move(callback);
        target->interval = 1.0 / std::max(1.0, framesPerSecond);
        target->menus = [[NSMutableSet alloc] init];
        auto* centre = [NSNotificationCenter defaultCenter];
        [centre addObserver:target selector:@selector(menuBeganTracking:) name:NSMenuDidBeginTrackingNotification object:nil];
        [centre addObserver:target selector:@selector(menuEndedTracking:) name:NSMenuDidEndTrackingNotification object:nil];
    }

    ~Impl() {
        [[NSNotificationCenter defaultCenter] removeObserver:target];
        [target stopTimer];
        [target->menus removeAllObjects];
        target->frame = {};
    }
};

EventTrackingFrames::EventTrackingFrames(std::function<void()> frame, double framesPerSecond)
    : impl_(std::make_unique<Impl>(std::move(frame), framesPerSecond)) {}

EventTrackingFrames::~EventTrackingFrames() = default;

void EventTrackingFrames::rethrowPendingException() {
    if (impl_ && impl_->target->failure) {
        std::rethrow_exception(std::exchange(impl_->target->failure, {}));
    }
}

void EventTrackingFrames::shutdown() {
    impl_.reset();
}

}

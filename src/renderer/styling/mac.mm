#include "renderer/styling/platform_styling.h"

#ifdef __APPLE__

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>

namespace Renderer::Styling {

bool allowsAutomaticResize(GLFWwindow* window) {
    if (window == nullptr) return false;
    NSWindow* nativeWindow = glfwGetCocoaWindow(window);
    return nativeWindow != nil && !(nativeWindow.styleMask & NSWindowStyleMaskFullScreen) &&
        !nativeWindow.isZoomed && !nativeWindow.inLiveResize;
}

void performAlignmentFeedback() {
    [[NSHapticFeedbackManager defaultPerformer] performFeedbackPattern:NSHapticFeedbackPatternAlignment
        performanceTime:NSHapticFeedbackPerformanceTimeNow];
}

void resizeWindow(GLFWwindow* window, int width, int height, int offsetX, bool preserveRightEdge) {
    if (window == nullptr) return;
    NSWindow* nativeWindow = glfwGetCocoaWindow(window);
    NSRect content = [nativeWindow contentRectForFrameRect:nativeWindow.frame];
    content.origin.x += offsetX;
    content.origin.y += content.size.height - height;
    content.size = NSMakeSize(width, height);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    CAMetalLayer* layer = [nativeWindow.contentView.layer isKindOfClass:[CAMetalLayer class]]
        ? (CAMetalLayer*)nativeWindow.contentView.layer : nil;
    layer.contentsGravity = preserveRightEdge ? kCAGravityTopRight : kCAGravityTopLeft;
    layer.contentsScale = nativeWindow.backingScaleFactor;
    [nativeWindow setFrame:[nativeWindow frameRectForContentRect:content] display:NO];
    [CATransaction commit];
}

std::optional<float> windowBottomCornerRadius(GLFWwindow* window, bool left) {
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 270000
    if (@available(macOS 27.0, *)) {
        if (window != nullptr) {
            NSWindow* nativeWindow = glfwGetCocoaWindow(window);
            NSViewCornerRadii* radii = nativeWindow.contentView.effectiveCornerRadii;
            if (radii != nil) return static_cast<float>(left ? radii.bottomLeft : radii.bottomRight);
        }
    }
#else
    (void)window;
    (void)left;
#endif
    return std::nullopt;
}

std::optional<float> windowTopCornerRadius(GLFWwindow* window, bool left) {
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 270000
    if (@available(macOS 27.0, *)) {
        if (window != nullptr) {
            NSWindow* nativeWindow = glfwGetCocoaWindow(window);
            NSViewCornerRadii* radii = nativeWindow.contentView.effectiveCornerRadii;
            if (radii != nil) return static_cast<float>(left ? radii.topLeft : radii.topRight);
        }
    }
#else
    (void)window;
    (void)left;
#endif
    return std::nullopt;
}

// https://developer.apple.com/documentation/quartzcore/cametallayer/colorspace
void applyPresentationColourSpace(GLFWwindow* window) {
    if (window == nullptr) return;
    NSWindow* nativeWindow = glfwGetCocoaWindow(window);
    NSView* view = nativeWindow.contentView;
    if (view == nil) return;
    view.wantsLayer = YES;
    CAMetalLayer* layer;
    if ([view.layer isKindOfClass:[CAMetalLayer class]]) layer = (CAMetalLayer*)view.layer;
    else {
        layer = [CAMetalLayer layer];
        view.layer = layer;
    }
    layer.contentsScale = nativeWindow.backingScaleFactor;
    layer.contentsGravity = kCAGravityTopLeft;
    CGColorSpaceRef colourSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    layer.colorspace = colourSpace;
    CGColorSpaceRelease(colourSpace);
}

void applyPlatformWindowStyling(GLFWwindow* window) {
    if (window == nullptr) {
        return;
    }

    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (nsWindow == nil) {
        return;
    }

    nsWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
    nsWindow.titlebarAppearsTransparent = YES;
    nsWindow.titleVisibility = NSWindowTitleHidden;
    nsWindow.opaque = NO;
    nsWindow.backgroundColor = [NSColor clearColor];

    NSView* originalContentView = nsWindow.contentView;
    if ([originalContentView isKindOfClass:[NSVisualEffectView class]]) {
        return;
    }

    NSVisualEffectView* visualEffectView = [[NSVisualEffectView alloc] initWithFrame:originalContentView.frame];
    if (@available(macOS 10.14, *)) {
        visualEffectView.material = NSVisualEffectMaterialUnderWindowBackground;
    } else {
        visualEffectView.material = NSVisualEffectMaterialTitlebar;
    }
    visualEffectView.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    visualEffectView.state = NSVisualEffectStateFollowsWindowActiveState;
    visualEffectView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    nsWindow.contentView = visualEffectView;
    [visualEffectView addSubview:originalContentView];
    originalContentView.translatesAutoresizingMaskIntoConstraints = NO;

    [NSLayoutConstraint activateConstraints:@[
        [originalContentView.topAnchor constraintEqualToAnchor:visualEffectView.topAnchor],
        [originalContentView.bottomAnchor constraintEqualToAnchor:visualEffectView.bottomAnchor],
        [originalContentView.leadingAnchor constraintEqualToAnchor:visualEffectView.leadingAnchor],
        [originalContentView.trailingAnchor constraintEqualToAnchor:visualEffectView.trailingAnchor]
    ]];

    [nsWindow invalidateShadow];
}

}

#endif

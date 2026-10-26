#include "renderer/styling/platform_styling.h"

#ifdef __APPLE__

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace Renderer::Styling {

bool allowsAutomaticResize(GLFWwindow* window) {
    if (window == nullptr) return false;
    NSWindow* nativeWindow = glfwGetCocoaWindow(window);
    return nativeWindow != nil && !(nativeWindow.styleMask & NSWindowStyleMaskFullScreen) &&
        !nativeWindow.isZoomed && !nativeWindow.inLiveResize;
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

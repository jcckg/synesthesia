#include "renderer/styling/platform_styling.h"

#ifdef __APPLE__

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <AppKit/AppKit.h>

namespace Renderer::Styling {

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

} // namespace Renderer::Styling

#endif

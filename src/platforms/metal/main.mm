#include "audio_input.h"
#include "colour_mapper.h"
#include "fft_processor.h"
#include "ui.h"
#include "system_theme_detector.h"
#include "ui/dragdrop/file_drop_manager.h"
#include "ui/input/trackpad_gestures.h"
#include "utilities/video/ffmpeg_locator.h"

#ifdef ENABLE_MIDI
#include "midi_input.h"
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_metal.h"
#include "implot.h"
#include "ui/icons.h"

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <Foundation/Foundation.h>

#include <cstdio>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <filesystem>

namespace {

std::string resolveResourceFile(const std::vector<std::string>& candidates) {
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

std::string getResourcesPath() {
    NSBundle *mainBundle = [NSBundle mainBundle];
    NSString *resourcesPath = [mainBundle resourcePath];
    return std::string([resourcesPath UTF8String]);
}

void windowStyling(GLFWwindow* window) {
    NSWindow* nsWindow = glfwGetCocoaWindow(window);

    nsWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
    nsWindow.titlebarAppearsTransparent = YES;
    nsWindow.titleVisibility = NSWindowTitleHidden;

    nsWindow.opaque = NO;
    nsWindow.backgroundColor = [NSColor clearColor];

    NSVisualEffectView* visualEffectView = [[NSVisualEffectView alloc] init];
    if (@available(macOS 10.14, *)) {
        visualEffectView.material = NSVisualEffectMaterialUnderWindowBackground;
    } else {
        visualEffectView.material = NSVisualEffectMaterialTitlebar;
    }

    visualEffectView.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    visualEffectView.state = NSVisualEffectStateFollowsWindowActiveState;
    
    NSView* originalContentView = nsWindow.contentView;
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

    CABasicAnimation* scaleAnimation = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
    scaleAnimation.fromValue = @0.95;
    scaleAnimation.toValue = @1.0;
    scaleAnimation.duration = 0.3;
    scaleAnimation.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
    [visualEffectView.layer addAnimation:scaleAnimation forKey:@"windowAppear"];
}

void getThemeBackgroundColour(float* colour) {
    SystemTheme theme = SystemThemeDetector::detectSystemTheme();
    if (theme == SystemTheme::Light) {
        colour[0] = 1.00f;
        colour[1] = 1.00f;
        colour[2] = 1.00f;
        colour[3] = 1.00f;
    } else {
        colour[0] = 0.00f;
        colour[1] = 0.00f;
        colour[2] = 0.00f;
        colour[3] = 1.00f;
    }
}

int app_main(int, char**) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    io.Fonts->AddFontDefault();

    std::string resourcesPath = getResourcesPath();
    const std::vector<std::string> fontCandidates = {
        resourcesPath + "/assets/fonts/IBMPlexMono-Medium.ttf",
        "../assets/fonts/IBMPlexMono-Medium.ttf",
        "assets/fonts/IBMPlexMono-Medium.ttf"
    };
    const std::vector<std::string> iconFontCandidates = {
        resourcesPath + "/assets/fonts/icons/" + std::string(FONT_ICON_FILE_NAME_FAS),
        "../assets/fonts/icons/" + std::string(FONT_ICON_FILE_NAME_FAS),
        "assets/fonts/icons/" + std::string(FONT_ICON_FILE_NAME_FAS)
    };

    std::string fontPath = resolveResourceFile(fontCandidates);
    std::string iconFontPath = resolveResourceFile(iconFontCandidates);

    ImFont* mainFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 15.0f);

    if (!mainFont) {
        fprintf(stderr, "Warning: Could not load IBMPlexMono. Falling back to default font.\n");
        mainFont = io.Fonts->AddFontDefault();
    }

    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = 16.0f;

    bool iconFontLoaded = false;
    ImFont* iconFont = io.Fonts->AddFontFromFileTTF(iconFontPath.c_str(), 16.0f, &icons_config, icons_ranges);
    if (iconFont != nullptr) {
        iconFontLoaded = true;
    }

    if (!iconFontLoaded) {
        fprintf(stderr, "Warning: Could not load Font Awesome icons. Icon buttons will not display correctly.\n");
    }

    io.FontDefault = mainFont;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    
    style.Alpha = 0.95f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    
    GLFWwindow* window = glfwCreateWindow(1480, 750, "Synesthesia", nullptr, nullptr);
    if (window == nullptr)
        return 1;

    windowStyling(window);

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
        std::cerr << "Metal: Failed to create default device\n";
        glfwTerminate();
        return 1;
    }

    id<MTLCommandQueue> commandQueue = [device newCommandQueue];
    if (commandQueue == nil) {
        std::cerr << "Metal: Failed to create command queue\n";
        glfwTerminate();
        return 1;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    FileDropManager::attach(window);
    TrackpadGestures::attach(window);
    ImGui_ImplMetal_Init(device);

    NSWindow* nswin = glfwGetCocoaWindow(window);
    NSView* metalContentView = nswin.contentView.subviews.firstObject;
    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.device = device;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalContentView.layer = metalLayer;
    metalContentView.wantsLayer = YES;

    MTLRenderPassDescriptor* renderPassDescriptor = [MTLRenderPassDescriptor new];

    AudioInput audioInput;
    std::vector<AudioInput::DeviceInfo> devices = AudioInput::getInputDevices();
    std::vector<AudioOutput::DeviceInfo> outputDevices = AudioOutput::getOutputDevices();

#ifdef ENABLE_MIDI
    MIDIInput midiInput;
    std::vector<MIDIInput::DeviceInfo> midiDevices = MIDIInput::getMIDIInputDevices();
#endif

    float clear_colour[4];
    getThemeBackgroundColour(clear_colour);

    auto& ffmpegLocator = Utilities::Video::FFmpegLocator::instance();
    ffmpegLocator.refresh();

    UIState uiState;

#ifdef ENABLE_MIDI
    uiState.midiDevicesAvailable = !midiDevices.empty();
    if (uiState.midiDevicesAvailable) {
        MIDIDeviceManager::populateMIDIDeviceNames(uiState.midiDeviceState, midiDevices);

        if (MIDIDeviceManager::autoDetectAndConnect(uiState.midiDeviceState, midiInput, midiDevices)) {
            uiState.midiAutoConnected = true;
            std::cout << "[MIDI] Auto-connected to MIDI device on startup" << std::endl;
        }
    }
#endif

    constexpr auto target_frame_duration = std::chrono::microseconds(8333);

    while (!glfwWindowShouldClose(window)) {
        auto frame_start = std::chrono::steady_clock::now();
        @autoreleasepool {
            glfwPollEvents();

            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            metalLayer.drawableSize = CGSizeMake(width, height);
            id<CAMetalDrawable> drawable = [metalLayer nextDrawable];

            if (!drawable) continue;

            id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

            renderPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(
                static_cast<double>(clear_colour[0]),
                static_cast<double>(clear_colour[1]),
                static_cast<double>(clear_colour[2]),
                static_cast<double>(clear_colour[3])
            );
            renderPassDescriptor.colorAttachments[0].texture = drawable.texture;
            renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
            renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;

            id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
            [renderEncoder pushDebugGroup:@"synesthesia"];

            ImGui_ImplMetal_NewFrame(renderPassDescriptor);
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            updateUI(audioInput, devices, outputDevices, clear_colour, io, uiState
#ifdef ENABLE_MIDI
                     , &midiInput, &midiDevices
#endif
                     );

            ImGui::Render();
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);

            [renderEncoder popDebugGroup];
            [renderEncoder endEncoding];
            [commandBuffer presentDrawable:drawable];
            [commandBuffer commit];
        }

        auto frame_end = std::chrono::steady_clock::now();
        auto frame_duration = frame_end - frame_start;
        if (frame_duration < target_frame_duration) {
            std::this_thread::sleep_for(target_frame_duration - frame_duration);
        }
    }

    ImGui_ImplMetal_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    TrackpadGestures::shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

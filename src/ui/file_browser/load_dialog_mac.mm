#include "ui/file_browser/file_browser.h"

#import <AppKit/AppKit.h>

#include "resyne/recorder/recorder.h"

@interface SynesthesiaLoadPanelDelegate : NSObject <NSOpenSavePanelDelegate>
@end

@implementation SynesthesiaLoadPanelDelegate
- (BOOL)panel:(id)sender shouldEnableURL:(NSURL*)url {
    (void)sender;
    NSNumber* directory = nil;
    [url getResourceValue:&directory forKey:NSURLIsDirectoryKey error:nil];
    if (directory.boolValue) return YES;
    const char* path = url.path.UTF8String;
    return path != nullptr && ReSyne::Recorder::isSupportedImportFile(path);
}
@end

namespace UI::FileBrowser {

std::string chooseLoadPath(const std::string& directory, bool allowDirectories, bool directoryOnly) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        __attribute__((objc_precise_lifetime)) SynesthesiaLoadPanelDelegate* delegate = [[SynesthesiaLoadPanelDelegate alloc] init];
        panel.delegate = delegate;
        panel.title = @"Load File or Directory";
        panel.prompt = @"Load";
        panel.canChooseFiles = !directoryOnly;
        panel.canChooseDirectories = allowDirectories || directoryOnly;
        panel.allowsMultipleSelection = NO;
        panel.resolvesAliases = YES;
        panel.treatsFilePackagesAsDirectories = NO;
        if (!directory.empty()) {
            panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:directory.c_str()]
                                           isDirectory:YES];
        }
        if ([panel runModal] != NSModalResponseOK || panel.URL == nil) return {};
        const char* path = panel.URL.path.UTF8String;
        return path != nullptr ? std::string(path) : std::string{};
    }
}

}

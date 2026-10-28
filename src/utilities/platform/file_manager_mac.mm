#include "utilities/platform/file_manager.h"

#include <filesystem>
#import <AppKit/AppKit.h>

namespace Utilities::FileManager {

std::string name() {
    return "Finder";
}

Result revealNative(const std::filesystem::path& path) {
    @autoreleasepool {
        const auto value = path.u8string();
        NSString* filename = [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
        if (filename == nil) return {false, "The selected item path is invalid."};
        NSURL* url = [NSURL fileURLWithPath:filename];
        if (url == nil) return {false, "The selected item path is invalid."};
        [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[url]];
        return {true, {}};
    }
}

}

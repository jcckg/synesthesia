#include "ui/help/help.h"

#import <AppKit/AppKit.h>

namespace UI::Help {

bool openProjectPage() {
    NSURL* url = [NSURL URLWithString:@"https://github.com/jcckg/synesthesia#readme"];
    return [[NSWorkspace sharedWorkspace] openURL:url];
}

}

#include "ui/file_browser/file_browser.h"

#ifndef __APPLE__

#include <portable-file-dialogs.h>

namespace UI::FileBrowser {

std::string chooseLoadPath(const std::string& directory, bool, bool directoryOnly) {
    if (directoryOnly) return pfd::select_folder("Load Directory", directory).result();
    auto files = pfd::open_file("Load Audio/Colour File", directory,
        {"Audio and Colour Files", "*.rsyn *.wav *.flac *.mp3 *.mpeg3 *.mpga *.ogg *.oga *.tiff *.tif",
         "All Files", "*"}).result();
    return files.empty() ? std::string{} : files.front();
}

}

#endif

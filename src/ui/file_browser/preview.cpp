#include "ui/file_browser/preview.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace UI::FileBrowser {

namespace {

std::filesystem::path path(const std::string& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

std::string utf8(const std::filesystem::path& value) {
    const auto text = value.u8string();
    return std::string(text.begin(), text.end());
}

}

void Preview::browse(std::string directory, bool previewDirectory) {
    if (!previewDirectory && selectedDirectory.empty()) selectedDirectory = browsingDirectory;
    browsingDirectory = std::move(directory);
    if (previewDirectory) {
        selectedDirectory.clear();
        interact();
    }
}

void Preview::select(std::string directory) {
    selectedDirectory = std::move(directory);
    interact();
}

void Preview::interact() {
    idleSeconds = 0.0f;
}

void Preview::focusTrack() {
    idleSeconds = idleDelaySeconds;
}

void Preview::update(const std::string& trackPath, bool trackAvailable, float deltaSeconds) {
    const float delta = std::max(0.0f, deltaSeconds);
    idleSeconds = std::min(idleDelaySeconds, idleSeconds + delta);
    showingTrack = trackAvailable && !trackPath.empty() && idleSeconds >= idleDelaySeconds;
    artworkDirectory = showingTrack ? utf8(path(trackPath).parent_path())
        : selectedDirectory.empty() ? browsingDirectory : selectedDirectory;
    const auto titlePath = showingTrack ? path(trackPath).stem() : path(artworkDirectory).filename();
    std::string nextTitle = titlePath.empty() ? artworkDirectory : utf8(titlePath);
    if (showingTrack) {
        const auto folder = utf8(path(artworkDirectory).filename());
        if (!folder.empty()) nextTitle += " - " + folder;
    }
    if (nextTitle != title) {
        previousTitle = std::move(title);
        title = nextTitle;
        titleBlend = previousTitle.empty() ? 1.0f : 0.0f;
    } else {
        titleBlend = std::min(1.0f, titleBlend + delta / 0.18f);
        if (titleBlend == 1.0f) previousTitle.clear();
    }
}

}

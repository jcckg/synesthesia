#pragma once

#include <string>

namespace UI::FileBrowser {

struct Preview {
    static constexpr float idleDelaySeconds = 3.0f;

    void browse(std::string directory, bool previewDirectory = true);
    void select(std::string directory);
    void interact();
    void focusTrack();
    void update(const std::string& trackPath, bool trackAvailable, float deltaSeconds);

    std::string browsingDirectory;
    std::string selectedDirectory;
    std::string artworkDirectory;
    std::string title;
    std::string previousTitle;
    bool showingTrack = false;
    float idleSeconds = 0.0f;
    float titleBlend = 1.0f;
};

}

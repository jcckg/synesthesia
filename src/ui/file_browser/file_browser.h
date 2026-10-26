#pragma once

#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include "ui/file_browser/preview.h"

struct UIState;

namespace UI::FileBrowser {

inline constexpr float panelWidth = 280.0f;
inline constexpr size_t maximumEntries = 100000;

class DirectoryScanner;
class Artwork;

struct Entry {
    std::string path;
    std::string name;
    std::string sortKey;
    bool directory = false;
};

struct State {
    State();
    ~State();
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    bool isOpen = false;
    bool onLeft = false;
    bool scanning = false;
    bool truncated = false;
    bool filterDirty = true;
    bool searchOpen = false;
    bool searchFocus = false;
    bool searchHadText = false;
    bool trackAvailable = false;
    bool loading = false;
    Preview preview;
    float artworkHeight = 0.0f;
    float entryPadding = 12.0f;
    float searchAnimation = 0.0f;
    std::string playingPath;
    ImVec4 playingColour;
    std::string artworkDirectory;
    std::string rootDirectory;
    std::string directory;
    std::string selectedPath;
    bool selectedDirectory = false;
    std::string requestedFile;
    std::string error;
    std::vector<Entry> files;
    std::vector<int> visibleFiles;
    ImGuiTextFilter filter;
    float titleElapsed = 0.0f;
    float previousTitleElapsed = 0.0f;
    std::unique_ptr<DirectoryScanner> scanner;
    std::unique_ptr<Artwork> artwork;
};

void selectDirectory(State& state, const std::string& path);
void navigate(State& state, const std::string& path, bool previewDirectory = true);
void refresh(State& state);
void update(State& state, float deltaSeconds);
void draw(State& state, bool canLoad, const ImVec2& displaySize);
bool canLoadFiles(UIState& state);
void uploadArtwork(State& state);
void releaseArtwork(State& state);
void drawToolbarButton(State& state, float height);
std::string chooseLoadPath(const std::string& directory, bool allowDirectories, bool directoryOnly = false);

}

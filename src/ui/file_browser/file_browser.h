#pragma once

#include <imgui.h>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "ui/file_browser/animation.h"
#include "ui/file_browser/preview.h"
#include "ui/input/scroll_motion.h"
#include "ui/file_browser/resizing.h"

struct UIState;

namespace UI::Input {
class Scrolling;
}

namespace UI::FileBrowser {

inline constexpr float panelWidth = Resizing::defaultWidth;
inline constexpr size_t maximumEntries = 100000;

class DirectoryScanner;
class Artwork;

struct Entry {
    std::string path;
    std::string name;
    std::string sortKey;
    bool directory = false;
};

struct DirectoryLocation {
    float scroll = 0.0f;
    std::string selection;
    std::string filter;
    uint64_t lastVisited = 0;
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
    bool trackAvailable = false;
    bool loading = false;
    Resizing resizing;
    float expandedWindowWidth = 0.0f;
    PanelAnimation panelAnimation;
    PanelAnimation resizeReveal{.value = 1.0f, .start = 1.0f, .target = 1.0f};
    Preview preview;
    Input::ScrollMotion scrolling;
    std::map<std::string, DirectoryLocation> locations;
    uint64_t navigationSequence = 0;
    float restoreScroll = 0.0f;
    bool restoreLocation = false;
    std::string hoveredEntry;
    float hoverElapsed = 0.0f;
    std::string animatedSelection;
    float selectionElapsed = 0.0f;
    bool entryHovered = false;
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

float visibleWidth(const State& state);
void advancePanel(State& state, float deltaSeconds, int frame);
void drawResizeHandle(State& state, const ImVec2& displaySize, float sidebarWidth, bool enabled);
void selectDirectory(State& state, const std::string& path);
void navigate(State& state, const std::string& path, bool previewDirectory = true);
void refresh(State& state);
void update(State& state, float deltaSeconds);
void draw(State& state, bool canLoad, const ImVec2& displaySize, Input::Scrolling& scrolling);
bool canLoadFiles(UIState& state);
void uploadArtwork(State& state);
void releaseArtwork(State& state);
void drawToolbarButton(State& state, float height);
std::string chooseLoadPath(const std::string& directory, bool allowDirectories, bool directoryOnly = false);

}

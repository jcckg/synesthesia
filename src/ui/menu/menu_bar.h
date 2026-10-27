#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>

namespace UI::Menu {

enum class Command {
    None,
    NewTimeline,
    Load,
    Export,
    ExportWAV,
    ExportRSYN,
    ExportMP4,
    ExportMP4WithGradient,
    ExportTIFF,
    ReSyneView,
    VisualiserView,
    DetachVisualiser,
    ToggleExplorer,
    SearchExplorer,
    ToggleOSC,
    ToggleInterface,
    ToggleSpectrumAnalyser,
    ToggleFollowScrubber,
    SwapSidebarSides,
    KeyboardShortcuts,
    ProjectPage
};

struct Item {
    const char* title = "";
    Command command = Command::None;
    const char* shortcut = "";
    std::span<const Item> children = {};
    bool showsSubtitle = false;
    bool shiftShortcut = false;
    bool isHelpMenu = false;
    const char* shortcutTitle = nullptr;
};

struct State {
    bool enabled = false;
    bool checked = false;
    std::string subtitle;
    std::string title;
};

class MenuBar {
public:
    using Query = std::function<State(Command)>;
    using Execute = std::function<void(Command)>;

    MenuBar(std::span<const Item> menus, Query query, Execute execute);
    ~MenuBar();

    MenuBar(const MenuBar&) = delete;
    MenuBar& operator=(const MenuBar&) = delete;

    void dispatchPendingCommands();
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

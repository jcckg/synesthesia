#include "ui/help/help.h"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <vector>

#include "controls.h"
#include "imgui.h"
#include "ui/menu/application_menu.h"

namespace UI::Help {

namespace {

struct Shortcut {
    const char* keys;
    const char* action;
};

void drawKeycaps(const char* keys, float rowHeight) {
    std::vector<std::string_view> tokens;
    std::string_view remaining(keys);
    while (!remaining.empty()) {
        const auto separator = remaining.find('+');
        auto token = remaining.substr(0, separator);
        while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
        while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
        if (!token.empty()) tokens.push_back(token);
        if (separator == std::string_view::npos) break;
        remaining.remove_prefix(separator + 1);
    }

    constexpr float padding = 5.0f;
    constexpr float spacing = 4.0f;
    float width = 0.0f;
    for (const auto token : tokens) {
        width += ImGui::CalcTextSize(token.data(), token.data() + token.size()).x + padding * 2.0f;
    }
    if (!tokens.empty()) width += spacing * static_cast<float>(tokens.size() - 1);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    float x = position.x + std::max(0.0f, ImGui::GetContentRegionAvail().x - width);
    const float height = ImGui::GetFontSize() + 6.0f;
    const float y = position.y + (rowHeight - height) * 0.5f;
    auto* drawList = ImGui::GetWindowDrawList();
    for (const auto token : tokens) {
        const ImVec2 size = ImGui::CalcTextSize(token.data(), token.data() + token.size());
        const ImVec2 min(x, y);
        const ImVec2 max(x + size.x + padding * 2.0f, y + height);
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
        drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border, 0.55f), ImGui::GetStyle().FrameRounding);
        drawList->AddText(ImVec2(x + padding, y + 3.0f), ImGui::GetColorU32(ImGuiCol_Text),
                          token.data(), token.data() + token.size());
        x = max.x + spacing;
    }
    ImGui::Dummy(ImVec2(width, rowHeight));
}

void drawSection(const char* title, std::span<const Shortcut> shortcuts, const char* note = nullptr) {
    if (Controls::renderSectionHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 2.0f));
        if (ImGui::BeginTable(title, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 13.0f);
            const float rowHeight = ImGui::GetFontSize() + 12.0f;
            for (const auto& shortcut : shortcuts) {
                ImGui::TableNextRow(0, rowHeight);
                ImGui::TableSetColumnIndex(0);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);
                ImGui::TextWrapped("%s", shortcut.action);
                ImGui::TableSetColumnIndex(1);
                drawKeycaps(shortcut.keys, rowHeight);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        if (note) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", note);
            ImGui::PopStyleColor();
        }
    }
    ImGui::Spacing();
}

}

void drawKeyboardShortcuts(State& state) {
    if (!state.showKeyboardShortcuts) return;

    constexpr const char* title = "Keyboard Shortcuts";
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(620.0f, viewport->WorkSize.x - 24.0f),
                                  std::min(620.0f, viewport->WorkSize.y - 24.0f)), ImGuiCond_Appearing);
    ImGui::OpenPopup(title);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    if (ImGui::BeginPopupModal(title, &state.showKeyboardShortcuts,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted(title);
        ImGui::TextDisabled("Quick access to views, playback and timeline tools.");
        ImGui::Spacing();

        constexpr float buttonHeight = 26.0f;
        const float footerHeight = buttonHeight + ImGui::GetStyle().ItemSpacing.y * 2.0f + 1.0f;
        if (ImGui::BeginChild("Shortcuts", ImVec2(0.0f, -footerHeight))) {
#ifdef __APPLE__
            const auto menuShortcuts = UI::Menu::menuShortcuts();
            std::vector<Shortcut> applicationShortcuts;
            applicationShortcuts.reserve(menuShortcuts.size());
            for (const auto& shortcut : menuShortcuts) {
                applicationShortcuts.push_back({shortcut.keys.c_str(), shortcut.action.c_str()});
            }
            drawSection("Application", applicationShortcuts);
#endif
            static constexpr std::array interfaceShortcuts{
                Shortcut{"H", "Show / hide interface"},
                Shortcut{"Shift + Tab", "Switch views"},
                Shortcut{"Space", "Play / pause"},
                Shortcut{"Esc", "Cancel / close"}
            };
            drawSection("Interface and playback", interfaceShortcuts,
                        "H applies in Visualiser View. Shift + Tab switches views while the interface is visible and the visualiser is attached.");
            static constexpr std::array timelineShortcuts{
                Shortcut{"1", "Cursor tool"},
                Shortcut{"2", "Zoom tool"},
                Shortcut{"3", "Grab tool"},
                Shortcut{"4", "Follow Scrubber"},
                Shortcut{"Cmd/Ctrl + drag", "Temporary zoom"},
                Shortcut{"Option/Alt + drag", "Temporary pan"}
            };
            drawSection("Timeline", timelineShortcuts,
                        "Focus the timeline to use these tools. Grab and pan require Follow Scrubber to be off and a zoomed timeline. Dragging with Grab or Option/Alt hides the cursor for continuous panning. Release or press Escape to restore it.");
        }
        ImGui::EndChild();

        ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - 80.0f));
        if (ImGui::Button("Close", ImVec2(80.0f, buttonHeight)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            state.showKeyboardShortcuts = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(4);
    if (!ImGui::IsPopupOpen(title)) state.showKeyboardShortcuts = false;
}

#ifndef __APPLE__
bool openProjectPage() {
    return false;
}
#endif

}

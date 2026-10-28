#include "ui/file_browser/context_menu.h"

#include <chrono>
#include <utility>
#include "imgui.h"
#include "ui/file_browser/file_browser.h"

namespace UI::FileBrowser {

ContextMenu::ContextMenu()
    : managerName_(std::async(std::launch::async, Utilities::FileManager::name)) {}

bool ContextMenu::attachToItem(const Entry& entry) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_AllowWhenBlockedByPopup) ||
        !ImGui::IsMouseReleased(ImGuiMouseButton_Right)) return false;
    path_ = entry.path;
    requested_ = true;
    return true;
}

void ContextMenu::update(std::string& error) {
    if (managerName_.valid() && managerName_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        label_ = "Reveal in " + managerName_.get();
    }
    if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = pending_.get();
        if (!result.success) error = std::move(result.error);
    }
}

void ContextMenu::draw(bool available) {
    constexpr const char* popup = "ExplorerEntryContext";
    if (requested_) {
        if (available) ImGui::OpenPopup(popup);
        requested_ = false;
    }
    if (!ImGui::BeginPopup(popup)) return;
    if (!available) ImGui::CloseCurrentPopup();
    else if (ImGui::MenuItem(label_.c_str(), nullptr, false, !pending_.valid())) {
        pending_ = std::async(std::launch::async, Utilities::FileManager::reveal, path_);
    }
    ImGui::EndPopup();
}

}

#pragma once

#include <future>
#include <string>
#include "utilities/platform/file_manager.h"

namespace UI::FileBrowser {

struct Entry;

class ContextMenu {
public:
    ContextMenu();
    bool attachToItem(const Entry& entry);
    void draw(bool available);
    void update(std::string& error);

private:
    std::string path_;
    std::string label_ = "Reveal in File Manager";
    bool requested_ = false;
    std::future<std::string> managerName_;
    std::future<Utilities::FileManager::Result> pending_;
};

}

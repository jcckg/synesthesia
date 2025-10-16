#include "file_drop_manager.h"

#include <mutex>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace {

std::mutex gQueueMutex;
std::vector<FileDropManager::FileDropEvent> gPendingEvents;

void dropCallback(GLFWwindow* window, int pathCount, const char** paths) {
    std::vector<std::string> collected;
    collected.reserve(static_cast<size_t>(pathCount));
    for (int i = 0; i < pathCount; ++i) {
        if (paths[i]) {
            collected.emplace_back(paths[i]);
        }
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    if (window != nullptr) {
        glfwGetCursorPos(window, &cursorX, &cursorY);
    }

    FileDropManager::enqueue(std::move(collected), cursorX, cursorY);
}

}

namespace FileDropManager {

void attach(GLFWwindow* window) {
    if (!window) {
        return;
    }
    glfwSetDropCallback(window, dropCallback);
}

void enqueue(std::vector<std::string> paths, double cursorX, double cursorY) {
    if (paths.empty()) {
        return;
    }

    FileDropEvent event;
    event.paths = std::move(paths);
    event.cursorX = cursorX;
    event.cursorY = cursorY;

    std::lock_guard<std::mutex> lock(gQueueMutex);
    gPendingEvents.push_back(std::move(event));
}

std::vector<FileDropEvent> consume() {
    std::lock_guard<std::mutex> lock(gQueueMutex);
    auto events = std::move(gPendingEvents);
    gPendingEvents.clear();
    return events;
}

}

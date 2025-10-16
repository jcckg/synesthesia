#pragma once

#include <string>
#include <vector>

struct GLFWwindow;

namespace FileDropManager {

struct FileDropEvent {
    std::vector<std::string> paths;
    double cursorX = 0.0;
    double cursorY = 0.0;
};

void attach(GLFWwindow* window);
void enqueue(std::vector<std::string> paths, double cursorX, double cursorY);
std::vector<FileDropEvent> consume();

}

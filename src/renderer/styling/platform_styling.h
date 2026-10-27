#pragma once

#include <optional>

struct GLFWwindow;

namespace Renderer::Styling {

void applyPlatformWindowStyling(GLFWwindow* window);
void applyPresentationColourSpace(GLFWwindow* window);

#ifdef __APPLE__
bool allowsAutomaticResize(GLFWwindow* window);
void performAlignmentFeedback();
void resizeWindow(GLFWwindow* window, int width, int height, int offsetX, bool preserveRightEdge);
std::optional<float> windowTopCornerRadius(GLFWwindow* window, bool left);
std::optional<float> windowBottomCornerRadius(GLFWwindow* window, bool left);
#endif

}

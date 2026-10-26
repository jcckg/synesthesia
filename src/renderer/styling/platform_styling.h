#pragma once

#include <optional>

struct GLFWwindow;

namespace Renderer::Styling {

void applyPlatformWindowStyling(GLFWwindow* window);
void applyPresentationColourSpace(GLFWwindow* window);

#ifdef __APPLE__
bool allowsAutomaticResize(GLFWwindow* window);
std::optional<float> windowTopCornerRadius(GLFWwindow* window, bool left);
std::optional<float> windowBottomCornerRadius(GLFWwindow* window, bool left);
#endif

}

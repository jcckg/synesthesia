#pragma once

#include <optional>

struct GLFWwindow;

namespace Renderer::Styling {

void applyPlatformWindowStyling(GLFWwindow* window);
void applyPresentationColourSpace(GLFWwindow* window);

#ifdef __APPLE__
std::optional<float> windowBottomCornerRadius(GLFWwindow* window, bool left);
#endif

}

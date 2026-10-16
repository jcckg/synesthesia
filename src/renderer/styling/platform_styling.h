#pragma once

struct GLFWwindow;

namespace Renderer::Styling {

void applyPlatformWindowStyling(GLFWwindow* window);
void applyPresentationColourSpace(GLFWwindow* window);

}

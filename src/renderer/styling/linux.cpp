#include "renderer/styling/platform_styling.h"

#if defined(__linux__) && !defined(__APPLE__)

namespace Renderer::Styling {

void applyPresentationColourSpace(GLFWwindow*) {}

void applyPlatformWindowStyling(GLFWwindow*) {
}

}

#endif

#include "renderer/styling/platform_styling.h"

#if defined(__linux__) && !defined(__APPLE__)

namespace Renderer::Styling {

void applyPlatformWindowStyling(GLFWwindow*) {
}

} // namespace Renderer::Styling

#endif

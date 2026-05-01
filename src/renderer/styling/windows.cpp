#include "renderer/styling/platform_styling.h"

#ifdef _WIN32

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <Windows.h>
#include <dwmapi.h>

#include "renderer/windows/resource.h"
#include "ui/styling/system_theme/system_theme_detector.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

namespace Renderer::Styling {

namespace {

void applyApplicationIcon(HWND hwnd) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    HICON bigIcon = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_ICON1),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR | LR_SHARED));
    HICON smallIcon = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_ICON1),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED));

    if (bigIcon != nullptr) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
        SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(bigIcon));
    }

    if (smallIcon != nullptr) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(smallIcon));
    }
}

}

void applyPlatformWindowStyling(GLFWwindow* window) {
    if (window == nullptr) {
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr) {
        return;
    }

    applyApplicationIcon(hwnd);

    const SystemTheme theme = SystemThemeDetector::detectSystemTheme();
    BOOL darkMode = theme == SystemTheme::Dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    COLORREF captionColour = theme == SystemTheme::Dark ? RGB(0, 0, 0) : RGB(255, 255, 255);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColour, sizeof(captionColour));
}

} // namespace Renderer::Styling

#endif

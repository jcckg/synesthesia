#include "system_theme_detector.h"

#ifdef _WIN32
#include <Windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>
using namespace winrt;
using namespace Windows::UI::ViewManagement;

SystemTheme SystemThemeDetector::detectSystemTheme() {
    try {
        init_apartment();
        UISettings uiSettings;
        auto background = uiSettings.GetColorValue(UIColorType::Background);
        double brightness = (0.299 * background.R + 0.587 * background.G + 0.114 * background.B) / 255.0;

        return brightness > 0.5 ? SystemTheme::Light : SystemTheme::Dark;
    } catch (const winrt::hresult_error&) {
        HKEY hKey;
        DWORD dwType = REG_DWORD;
        DWORD dwSize = sizeof(DWORD);
        DWORD dwValue = 1;

        if (RegOpenKeyEx(HKEY_CURRENT_USER,
                        TEXT("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueEx(hKey, TEXT("AppsUseLightTheme"), NULL, &dwType,
                          reinterpret_cast<LPBYTE>(&dwValue), &dwSize);
            RegCloseKey(hKey);
        }

        return dwValue ? SystemTheme::Light : SystemTheme::Dark;
    }
}

bool SystemThemeDetector::isSystemInDarkMode() {
    return detectSystemTheme() == SystemTheme::Dark;
}

#elif !defined(__APPLE__)
SystemTheme SystemThemeDetector::detectSystemTheme() {
    return SystemTheme::Dark;
}

bool SystemThemeDetector::isSystemInDarkMode() {
    return true;
}

#endif
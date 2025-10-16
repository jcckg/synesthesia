#pragma once

enum class SystemTheme {
    Light,
    Dark,
    Unknown
};

class SystemThemeDetector {
public:
    static SystemTheme detectSystemTheme();
    static bool isSystemInDarkMode();
};

#include "renderer/font_loader.h"

#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"

#include "ui/icons.h"

#if defined(_WIN32)
#include "renderer/windows/resource.h"
#include "renderer/windows/resource_loader.h"
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace Renderer {

namespace {

std::string resolveResourceFile(const std::vector<std::string>& candidates) {
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

#if defined(__APPLE__)
std::string getBundleResourcesPath() {
    std::vector<char> pathBuffer(1024);
    uint32_t length = static_cast<uint32_t>(pathBuffer.size());

    if (_NSGetExecutablePath(pathBuffer.data(), &length) != 0) {
        pathBuffer.resize(length);
        if (_NSGetExecutablePath(pathBuffer.data(), &length) != 0) {
            return {};
        }
    }

    std::error_code error;
    const std::filesystem::path executablePath = std::filesystem::weakly_canonical(pathBuffer.data(), error);
    if (error) {
        return {};
    }

    const std::filesystem::path resourcesPath = executablePath.parent_path().parent_path() / "Resources";
    if (std::filesystem::is_directory(resourcesPath)) {
        return resourcesPath.string();
    }

    return {};
}
#endif

} // namespace

bool loadFonts(ImGuiIO& io, float dpiScale) {
    ImFont* mainFont = nullptr;

#if defined(__APPLE__)
    const std::string bundleResourcesPath = getBundleResourcesPath();
#endif

#if defined(_WIN32)
    try {
        static std::vector<unsigned char> textFontData = ResourceLoader::loadResourceAsVector(IDR_FONT_IBM_PLEX_MONO_MEDIUM);
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        mainFont = io.Fonts->AddFontFromMemoryTTF(
            textFontData.data(),
            static_cast<int>(textFontData.size()),
            15.0f * dpiScale,
            &config
        );
    } catch (const std::exception&) {
    }
#endif

    if (mainFont == nullptr) {
        std::vector<std::string> candidates;
#if defined(__APPLE__)
        if (!bundleResourcesPath.empty()) {
            candidates.push_back(bundleResourcesPath + "/assets/fonts/IBMPlexMono-Medium.ttf");
        }
#endif
        candidates.push_back("../assets/fonts/IBMPlexMono-Medium.ttf");
        candidates.push_back("assets/fonts/IBMPlexMono-Medium.ttf");
        candidates.push_back("IBMPlexMono-Medium.ttf");

        const std::string path = resolveResourceFile(candidates);
        if (!path.empty()) {
            mainFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 15.0f * dpiScale);
        }
    }

    if (mainFont == nullptr) {
        mainFont = io.Fonts->AddFontDefault();
    }

    io.FontDefault = mainFont;

    static const ImWchar iconRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    iconConfig.GlyphMinAdvanceX = 16.0f * dpiScale;

    bool iconFontLoaded = false;

#if defined(_WIN32)
    try {
        static std::vector<unsigned char> iconFontData = ResourceLoader::loadResourceAsVector(IDR_FONT_AWESOME_SOLID);
        ImFontConfig config = iconConfig;
        config.FontDataOwnedByAtlas = false;
        ImFont* iconFont = io.Fonts->AddFontFromMemoryTTF(
            iconFontData.data(),
            static_cast<int>(iconFontData.size()),
            16.0f * dpiScale,
            &config,
            iconRanges
        );
        iconFontLoaded = iconFont != nullptr;
    } catch (const std::exception&) {
    }
#endif

    if (!iconFontLoaded) {
        std::vector<std::string> candidates;
#if defined(__APPLE__)
        if (!bundleResourcesPath.empty()) {
            candidates.push_back(bundleResourcesPath + "/assets/fonts/icons/" + std::string(FONT_ICON_FILE_NAME_FAS));
        }
#endif
        candidates.push_back(std::string("../assets/fonts/icons/") + FONT_ICON_FILE_NAME_FAS);
        candidates.push_back(std::string("assets/fonts/icons/") + FONT_ICON_FILE_NAME_FAS);
        candidates.push_back(FONT_ICON_FILE_NAME_FAS);

        const std::string path = resolveResourceFile(candidates);
        if (!path.empty()) {
            iconConfig.FontDataOwnedByAtlas = true;
            ImFont* iconFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f * dpiScale, &iconConfig, iconRanges);
            iconFontLoaded = iconFont != nullptr;
        }
    }

    return io.FontDefault != nullptr;
}

} // namespace Renderer

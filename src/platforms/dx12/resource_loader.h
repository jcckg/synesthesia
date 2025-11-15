#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <filesystem>
#include <fstream>

namespace ResourceLoader {

struct ResourceData {
    const void* data;
    size_t size;
};

inline ResourceData loadResource(int resourceId, const char* resourceType = "RCDATA") {
    HMODULE hModule = GetModuleHandle(nullptr);

    HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(resourceId), resourceType);
    if (!hResource) {
        throw std::runtime_error("Failed to find resource");
    }

    HGLOBAL hLoadedResource = LoadResource(hModule, hResource);
    if (!hLoadedResource) {
        throw std::runtime_error("Failed to load resource");
    }

    void* pResourceData = LockResource(hLoadedResource);
    if (!pResourceData) {
        throw std::runtime_error("Failed to lock resource");
    }

    DWORD resourceSize = SizeofResource(hModule, hResource);

    ResourceData result;
    result.data = pResourceData;
    result.size = static_cast<size_t>(resourceSize);

    return result;
}

inline std::vector<unsigned char> loadResourceAsVector(int resourceId, const char* resourceType = "RCDATA") {
    ResourceData resource = loadResource(resourceId, resourceType);

    const unsigned char* dataPtr = static_cast<const unsigned char*>(resource.data);
    return std::vector<unsigned char>(dataPtr, dataPtr + resource.size);
}

inline std::string loadResourceAsString(int resourceId, const char* resourceType = "RCDATA") {
    ResourceData resource = loadResource(resourceId, resourceType);

    const char* dataPtr = static_cast<const char*>(resource.data);
    return std::string(dataPtr, resource.size);
}

inline std::string extractResourceToTemp(int resourceId, const std::string& filename, const char* resourceType = "RCDATA") {
    ResourceData resource = loadResource(resourceId, resourceType);

    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "synesthesia";
    std::filesystem::create_directories(tempDir);

    std::filesystem::path filePath = tempDir / filename;

    if (std::filesystem::exists(filePath)) {
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(filePath, ec);
        if (!ec && fileSize == resource.size) {
            return filePath.string();
        }
    }

    std::ofstream outFile(filePath, std::ios::binary);
    if (!outFile) {
        throw std::runtime_error("Failed to create temporary file");
    }

    outFile.write(static_cast<const char*>(resource.data), static_cast<std::streamsize>(resource.size));
    outFile.close();

    if (!outFile) {
        throw std::runtime_error("Failed to write resource to temporary file");
    }

    return filePath.string();
}

} // namespace ResourceLoader

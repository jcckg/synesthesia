#include "update.h"
#include <imgui.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <regex>
#include <cstdlib>
#include <memory>
#include <array>
#include <algorithm> 
#include <vector>

#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
    #include <shellapi.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
#elif __APPLE__
    #include <CoreFoundation/CoreFoundation.h>
    #include <ApplicationServices/ApplicationServices.h>
    #include <unistd.h>
    #include <sys/wait.h>
#endif

#include <nlohmann/json.hpp>

struct UpdateChecker::Impl {
    std::atomic<bool> requestInProgress{false};
    std::atomic<bool> updateCheckComplete{false};
    std::atomic<bool> shutdownRequested{false};
    std::mutex dataMutex;
    
    std::string latestVersionFound;
    std::string downloadUrlFound;
    bool updateFoundFlag = false;

    std::thread updateThread;
    std::mutex threadMutex;
};

UpdateChecker::UpdateChecker() : pImpl(std::make_unique<Impl>()) {}
UpdateChecker::~UpdateChecker() {
    if (pImpl) {
        pImpl->shutdownRequested = true;
        
        std::lock_guard<std::mutex> lock(pImpl->threadMutex);
        if (pImpl->updateThread.joinable()) {
            pImpl->updateThread.join();
        }
    }
}

bool UpdateChecker::isSupportedPlatform() const {
#ifdef _WIN32
    return true;
#elif __APPLE__
    #if defined(__arm64__) || defined(__aarch64__)
    return true;
    #else
    return false;
    #endif
#else
    return false;
#endif
}

std::string UpdateChecker::getPlatformString() const {
#ifdef _WIN32
    return "windows";
#elif __APPLE__
    return "macos";
#else
    return "unknown";
#endif
}

bool UpdateChecker::isNewerVersion(const std::string& current, const std::string& latest) const {
    auto parseVersion = [](const std::string& version) {
        std::vector<int> parts;
        std::stringstream ss(version);
        std::string part;
        
        std::string cleanVersion = version;
        if (!cleanVersion.empty() && cleanVersion[0] == 'v') {
            cleanVersion = cleanVersion.substr(1);
        }
        
        std::stringstream cleanSs(cleanVersion);
        while (std::getline(cleanSs, part, '.')) {
            try {
                parts.push_back(std::stoi(part));
            } catch (const std::invalid_argument&) {
                parts.push_back(0);
            } catch (const std::out_of_range&) {
                parts.push_back(0);
            }
        }
        
        while (parts.size() < 3) {
            parts.push_back(0);
        }
        
        return parts;
    };
    
    auto currentParts = parseVersion(current);
    auto latestParts = parseVersion(latest);

    size_t minSize = std::min(currentParts.size(), latestParts.size());
    for (size_t i = 0; i < minSize; ++i) {
        if (latestParts[i] > currentParts[i]) {
            return true;
        } else if (latestParts[i] < currentParts[i]) {
            return false;
        }
    }
    
    return false;
}

void UpdateChecker::checkForUpdates(const std::string& repoOwner, const std::string& repoName) {
    if (!isSupportedPlatform() || pImpl->requestInProgress.load()) {
        return;
    }
    
    pImpl->requestInProgress = true;
    pImpl->updateCheckComplete = false;
    
    std::string apiUrl = "https://api.github.com/repos/" + repoOwner + "/" + repoName + "/releases/latest";
    
    auto implPtr = pImpl.get();
    {
        std::lock_guard<std::mutex> lock(implPtr->threadMutex);
        if (implPtr->updateThread.joinable()) {
            implPtr->updateThread.join();
        }
        
        implPtr->updateThread = std::thread([implPtr, apiUrl, this]() {
            if (implPtr->shutdownRequested) return;
            
            performHttpRequest(apiUrl, [implPtr](const std::string& response) {
                if (implPtr->shutdownRequested) return;
                
                std::lock_guard<std::mutex> lock(implPtr->dataMutex);
                
                if (implPtr->shutdownRequested) return;
                
                try {
                    auto json = nlohmann::json::parse(response);
                    
                    if (json.contains("tag_name") && json.contains("html_url")) {
                        std::string latestVersion = json["tag_name"];
                        std::string htmlUrl = json["html_url"];
                        
                        implPtr->latestVersionFound = latestVersion;
                        implPtr->downloadUrlFound = htmlUrl;
                        implPtr->updateFoundFlag = true;
                    }
                } catch (const std::exception& e) {
                    implPtr->updateFoundFlag = false;
                }
                
                implPtr->updateCheckComplete = true;
                implPtr->requestInProgress = false;
            });
        });
    }
}

std::string executeCommand(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    
#ifdef _WIN32
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd, "r"), _pclose);
#else
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
#endif
    
    if (!pipe) {
        return "";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    return result;
}

void UpdateChecker::performHttpRequest(const std::string& url, std::function<void(const std::string&)> callback) {
#ifdef _WIN32
    HINTERNET hSession = WinHttpOpen(L"UpdateChecker/1.0", 
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, 
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    
    if (!hSession) {
        callback("");
        return;
    }
    
    std::wstring wUrl(url.begin(), url.end());
    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    
    wchar_t hostname[256] = {0};
    wchar_t path[1024] = {0};
    
    urlComp.lpszHostName = hostname;
    urlComp.dwHostNameLength = sizeof(hostname)/sizeof(wchar_t);
    urlComp.lpszUrlPath = path;
    urlComp.dwUrlPathLength = sizeof(path)/sizeof(wchar_t);
    
    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &urlComp)) {
        WinHttpCloseHandle(hSession);
        callback("");
        return;
    }
    
    HINTERNET hConnect = WinHttpConnect(hSession, hostname, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        callback("");
        return;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL, 
                                           WINHTTP_NO_REFERER, 
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        callback("");
        return;
    }
    
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, NULL)) {
        
        std::string response;
        DWORD bytesAvailable = 0;
        
        do {
            if (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
                std::vector<char> buffer(bytesAvailable, 0);
                DWORD bytesRead = 0;
                
                if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
                    response.append(buffer.data(), bytesRead);
                }
            }
        } while (bytesAvailable > 0);
        
        callback(response);
    } else {
        callback("");
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
#elif __APPLE__
    std::string command = "curl -s -L --max-time 10 \"" + url + "\"";
    std::string response = executeCommand(command.c_str());
    callback(response);
#endif
}

void UpdateChecker::update(UpdateState& state) {
    if (pImpl->updateCheckComplete.load()) {
        std::lock_guard<std::mutex> lock(pImpl->dataMutex);
        
        if (pImpl->updateFoundFlag) {
            bool isNewer = isNewerVersion(state.currentVersion, pImpl->latestVersionFound);
            
            if (isNewer) {
                state.updateAvailable = true;
                state.latestVersion = pImpl->latestVersionFound;
                state.downloadUrl = pImpl->downloadUrlFound;
                state.shouldShowBanner = true;
                state.updatePromptVisible = true;
            }
        }
        
        pImpl->updateCheckComplete = false;
        pImpl->updateFoundFlag = false;
    }
    
    state.checkingForUpdate = pImpl->requestInProgress.load();
}

bool UpdateChecker::shouldShowUpdateBanner(const UpdateState& state) const {
    return state.shouldShowBanner && state.updatePromptVisible && state.updateAvailable;
}

void UpdateChecker::drawUpdateBanner(UpdateState& state, float, float) {
    if (!shouldShowUpdateBanner(state)) {
        return;
    }

    const float popupWidth = 280.0f;
    const float popupHeight = 70.0f;
    const float marginRight = 20.0f;
    const float marginBottom = 53.0f;

    ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    float posX = windowSize.x - popupWidth - marginRight;
    float posY = windowSize.y - popupHeight - marginBottom;

    ImGui::SetNextWindowPos(ImVec2(posX, posY));
    ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.25f, 0.45f, 0.95f));

    ImGui::Begin("##UpdateBanner", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoCollapse);

    ImGui::SetCursorPosY(8.0f);
    ImGui::Text("Update Available");

    ImGui::SetCursorPosY(26.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.85f, 0.9f, 1.0f));
    ImGui::TextWrapped("Version %s is available", state.latestVersion.c_str());
    ImGui::PopStyleColor();

    ImGui::SetCursorPosY(popupHeight - 28.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.6f, 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.4f, 0.7f, 1.0f));

    if (ImGui::Button("Download", ImVec2(100, 20))) {
        openDownloadUrl(state.downloadUrl);
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::SetCursorPosX(popupWidth - 70);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.3f, 0.4f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.4f, 0.5f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.25f, 0.35f, 1.0f));

    if (ImGui::Button("Dismiss", ImVec2(60, 20))) {
        state.updatePromptVisible = false;
        state.shouldShowBanner = false;
    }
    ImGui::PopStyleColor(3);

    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void UpdateChecker::openDownloadUrl(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif __APPLE__
    std::string command = "open \"" + url + "\"";
    int result = system(command.c_str());
    if (result != 0) {
    }
#endif
}

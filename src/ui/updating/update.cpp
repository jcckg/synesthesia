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
#include <iostream>

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
    #include <fcntl.h>
    #include <signal.h>
#endif

#include <nlohmann/json.hpp>

namespace {

bool isValidGitHubUrl(const std::string& url) {
    if (url.length() > 2048) {
        std::cerr << "[UpdateChecker] URL rejected: exceeds maximum length" << std::endl;
        return false;
    }

    for (char c : url) {
        if (c == '\r' || c == '\n' || c == '\0') {
            std::cerr << "[UpdateChecker] URL rejected: contains control characters" << std::endl;
            return false;
        }
    }

    static const std::regex githubReleasesPattern(
        R"(^https://github\.com/[a-zA-Z0-9_.-]+/[a-zA-Z0-9_.-]+/releases/)"
    );

    if (!std::regex_search(url, githubReleasesPattern)) {
        std::cerr << "[UpdateChecker] URL rejected: not a valid GitHub releases URL" << std::endl;
        return false;
    }

    return true;
}

#ifdef __APPLE__
std::pair<bool, std::string> executeProcessSafely(
    const std::vector<std::string>& args,
    int timeoutSeconds = 10
) {
    if (args.empty()) {
        return {false, ""};
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        std::cerr << "[UpdateChecker] Failed to create pipe" << std::endl;
        return {false, ""};
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[UpdateChecker] Failed to fork process" << std::endl;
        close(pipefd[0]);
        close(pipefd[1]);
        return {false, ""};
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        std::vector<char*> argv;
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);

    int flags = fcntl(pipefd[0], F_GETFL);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    std::string output;
    char buffer[4096];
    int elapsedMs = 0;
    const int pollIntervalMs = 50;
    const int timeoutMs = timeoutSeconds * 1000;

    while (elapsedMs < timeoutMs) {
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);

        if (result == pid) {
            ssize_t bytesRead;
            while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytesRead] = '\0';
                output += buffer;
            }
            close(pipefd[0]);
            return {WIFEXITED(status) && WEXITSTATUS(status) == 0, output};
        }

        if (result < 0) {
            close(pipefd[0]);
            return {false, ""};
        }

        ssize_t bytesRead = read(pipefd[0], buffer, sizeof(buffer) - 1);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            output += buffer;
        }

        usleep(pollIntervalMs * 1000);
        elapsedMs += pollIntervalMs;
    }

    std::cerr << "[UpdateChecker] Process timed out after " << timeoutSeconds << " seconds" << std::endl;
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    close(pipefd[0]);
    return {false, ""};
}
#endif

}

struct UpdateChecker::Impl {
    std::atomic<bool> requestInProgress{false};
    std::atomic<bool> updateCheckComplete{false};
    std::atomic<bool> shutdownRequested{false};
    std::mutex dataMutex;

    std::string latestVersionFound;
    std::string downloadUrlFound;
    bool updateFoundFlag = false;

    UpdateError lastError = UpdateError::None;
    std::string lastErrorMessage;

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

                if (response.empty()) {
                    std::cerr << "[UpdateChecker] Empty response from API" << std::endl;
                    implPtr->lastError = UpdateError::NetworkError;
                    implPtr->lastErrorMessage = "Failed to fetch update information";
                    implPtr->updateFoundFlag = false;
                    implPtr->updateCheckComplete = true;
                    implPtr->requestInProgress = false;
                    return;
                }

                try {
                    auto json = nlohmann::json::parse(response);

                    if (json.contains("tag_name") && json.contains("html_url")) {
                        std::string latestVersion = json["tag_name"];
                        std::string htmlUrl = json["html_url"];

                        if (!isValidGitHubUrl(htmlUrl)) {
                            implPtr->lastError = UpdateError::InvalidUrl;
                            implPtr->lastErrorMessage = "Invalid download URL in response";
                            implPtr->updateFoundFlag = false;
                        } else {
                            implPtr->latestVersionFound = latestVersion;
                            implPtr->downloadUrlFound = htmlUrl;
                            implPtr->updateFoundFlag = true;
                            implPtr->lastError = UpdateError::None;
                            implPtr->lastErrorMessage.clear();
                        }
                    } else {
                        std::cerr << "[UpdateChecker] Response missing required fields" << std::endl;
                        implPtr->lastError = UpdateError::InvalidResponse;
                        implPtr->lastErrorMessage = "Invalid response format";
                        implPtr->updateFoundFlag = false;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[UpdateChecker] JSON parse error: " << e.what() << std::endl;
                    implPtr->lastError = UpdateError::InvalidResponse;
                    implPtr->lastErrorMessage = std::string("Parse error: ") + e.what();
                    implPtr->updateFoundFlag = false;
                }

                implPtr->updateCheckComplete = true;
                implPtr->requestInProgress = false;
            });
        });
    }
}


void UpdateChecker::performHttpRequest(const std::string& url, std::function<void(const std::string&)> callback) {
#ifdef _WIN32
    HINTERNET hSession = WinHttpOpen(L"UpdateChecker/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) {
        std::cerr << "[UpdateChecker] Failed to open WinHttp session" << std::endl;
        callback("");
        return;
    }

    DWORD timeout = 10000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

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
        std::cerr << "[UpdateChecker] Failed to parse URL" << std::endl;
        WinHttpCloseHandle(hSession);
        callback("");
        return;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostname, urlComp.nPort, 0);
    if (!hConnect) {
        std::cerr << "[UpdateChecker] Failed to connect to server" << std::endl;
        WinHttpCloseHandle(hSession);
        callback("");
        return;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);

    if (!hRequest) {
        std::cerr << "[UpdateChecker] Failed to open request" << std::endl;
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
        std::cerr << "[UpdateChecker] HTTP request failed" << std::endl;
        callback("");
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

#elif __APPLE__
    std::vector<std::string> args = {
        "/usr/bin/curl", "-s", "-L", "--max-time", "10", "--", url
    };

    auto [success, response] = executeProcessSafely(args, 10);
    if (!success) {
        std::cerr << "[UpdateChecker] curl request failed" << std::endl;
    }
    callback(response);
#endif
}

void UpdateChecker::update(UpdateState& state) {
    if (pImpl->updateCheckComplete.load()) {
        std::lock_guard<std::mutex> lock(pImpl->dataMutex);

        state.lastError = pImpl->lastError;
        state.lastErrorMessage = pImpl->lastErrorMessage;

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
        pImpl->lastError = UpdateError::None;
        pImpl->lastErrorMessage.clear();
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

    const float popupWidth = 220.0f;
    const float popupHeight = 110.0f;
    const float marginRight = 20.0f;
    const float marginBottom = 53.0f;

    ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    float posX = windowSize.x - popupWidth - marginRight;
    float posY = windowSize.y - popupHeight - marginBottom;

    ImGui::SetNextWindowPos(ImVec2(posX, posY));
    ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight));

    ImVec4 bgColor = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    bgColor.w = 0.97f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 14));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bgColor);

    ImGui::Begin("##UpdateBanner", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Update Available");
    ImGui::Spacing();
    ImGui::TextWrapped("Version %s is available", state.latestVersion.c_str());
    ImGui::Spacing();

    if (ImGui::Button("Download", ImVec2(100, 22))) {
        openDownloadUrl(state.downloadUrl);
    }

    ImGui::SameLine(0, 8);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 0.7f));
    if (ImGui::Button("Dismiss", ImVec2(70, 22))) {
        state.updatePromptVisible = false;
        state.shouldShowBanner = false;
    }
    ImGui::PopStyleColor(3);

    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void UpdateChecker::openDownloadUrl(const std::string& url) {
    if (!isValidGitHubUrl(url)) {
        std::cerr << "[UpdateChecker] Refusing to open invalid URL" << std::endl;
        return;
    }

#ifdef _WIN32
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif __APPLE__
    std::vector<std::string> args = {"/usr/bin/open", "--", url};

    auto [success, output] = executeProcessSafely(args, 5);
    if (!success) {
        std::cerr << "[UpdateChecker] Failed to open URL in browser" << std::endl;
    }
#endif
}

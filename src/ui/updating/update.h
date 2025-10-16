#pragma once

#include <string>
#include <functional>
#include <memory>
#include "version.h"

struct UpdateState {
    bool updateAvailable = false;
    bool checkingForUpdate = false;
    bool updatePromptVisible = false;
    bool hasCheckedThisSession = false;
    std::string latestVersion;
    std::string downloadUrl;
    std::string currentVersion = SYNESTHESIA_VERSION_STRING;
    
    bool shouldShowBanner = false;
    float bannerHeight = 35.0f;
};

class UpdateChecker {
public:
    UpdateChecker();
    ~UpdateChecker();

    void checkForUpdates(const std::string& repoOwner, const std::string& repoName);
    void update(UpdateState& state);
    
    bool shouldShowUpdateBanner(const UpdateState& state) const;
    
    void drawUpdateBanner(UpdateState& state, float windowWidth, float sidebarWidth);
    void openDownloadUrl(const std::string& url);
    
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;

    bool isSupportedPlatform() const;
    std::string getPlatformString() const;
    
    bool isNewerVersion(const std::string& current, const std::string& latest) const;
    
    void performHttpRequest(const std::string& url, std::function<void(const std::string&)> callback);
};

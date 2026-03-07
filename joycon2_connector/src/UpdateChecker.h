#pragma once
// UpdateChecker - Async GitHub release version checker (non-blocking)
// Uses WinRT Windows.Web.Http for network requests with timeout handling.

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <chrono>
#include <shellapi.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Storage.Streams.h>

#include "version.h"

enum class UpdateState {
    Idle,
    Checking,
    UpdateAvailable,
    UpToDate,
    Error
};

class UpdateChecker {
public:
    static UpdateChecker& Instance() {
        static UpdateChecker inst;
        return inst;
    }

    // Launch async check (non-blocking, runs on background thread)
    void CheckForUpdate() {
        UpdateState expected = UpdateState::Checking;
        if (state_.load() == expected) return;  // already checking

        state_.store(UpdateState::Checking);
        manualCheck_ = true;

        std::thread([this]() {
            try {
                // Create HTTP client with timeout
                winrt::Windows::Web::Http::HttpClient client;
                auto headers = client.DefaultRequestHeaders();
                headers.UserAgent().TryParseAdd(L"joycon2-connector");

                winrt::Windows::Foundation::Uri uri(L"https://api.github.com/repos/Misaka10571/joycon2-connector/releases/latest");

                // Send GET request with timeout (10 seconds)
                auto asyncOp = client.GetStringAsync(uri);

                // Wait with timeout using std::future-like approach
                auto status = asyncOp.wait_for(std::chrono::seconds(10));
                if (status != winrt::Windows::Foundation::AsyncStatus::Completed) {
                    // Timed out or error — cancel the operation
                    asyncOp.Cancel();
                    state_.store(UpdateState::Error);
                    return;
                }

                winrt::hstring responseBody = asyncOp.GetResults();
                std::string json = winrt::to_string(responseBody);

                // Extract "tag_name" from JSON response
                std::string tagName = ExtractTagName(json);
                if (tagName.empty()) {
                    state_.store(UpdateState::Error);
                    return;
                }

                // Strip leading 'v' or 'V' if present
                if (!tagName.empty() && (tagName[0] == 'v' || tagName[0] == 'V')) {
                    tagName = tagName.substr(1);
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    latestVersion_ = tagName;
                }

                // Compare versions
                if (IsNewerVersion(tagName)) {
                    state_.store(UpdateState::UpdateAvailable);
                    showPopup_ = true;
                } else {
                    state_.store(UpdateState::UpToDate);
                }

            } catch (...) {
                // Any exception (network error, WinRT error, etc.)
                state_.store(UpdateState::Error);
            }
        }).detach();
    }

    // Launch check silently (for auto-check on startup — only shows popup if update found)
    void CheckForUpdateSilent() {
        manualCheck_ = false;
        UpdateState expected = UpdateState::Checking;
        if (state_.load() == expected) return;

        state_.store(UpdateState::Checking);

        std::thread([this]() {
            try {
                winrt::Windows::Web::Http::HttpClient client;
                auto headers = client.DefaultRequestHeaders();
                headers.UserAgent().TryParseAdd(L"joycon2-connector");

                winrt::Windows::Foundation::Uri uri(L"https://api.github.com/repos/Misaka10571/joycon2-connector/releases/latest");

                auto asyncOp = client.GetStringAsync(uri);
                auto status = asyncOp.wait_for(std::chrono::seconds(10));
                if (status != winrt::Windows::Foundation::AsyncStatus::Completed) {
                    asyncOp.Cancel();
                    state_.store(UpdateState::Idle);  // Silent fail
                    return;
                }

                winrt::hstring responseBody = asyncOp.GetResults();
                std::string json = winrt::to_string(responseBody);

                std::string tagName = ExtractTagName(json);
                if (tagName.empty()) {
                    state_.store(UpdateState::Idle);
                    return;
                }

                if (!tagName.empty() && (tagName[0] == 'v' || tagName[0] == 'V')) {
                    tagName = tagName.substr(1);
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    latestVersion_ = tagName;
                }

                if (IsNewerVersion(tagName)) {
                    state_.store(UpdateState::UpdateAvailable);
                    showPopup_ = true;
                } else {
                    state_.store(UpdateState::Idle);  // Silent — don't show "up to date"
                }

            } catch (...) {
                state_.store(UpdateState::Idle);  // Silent fail on auto-check
            }
        }).detach();
    }

    UpdateState GetState() const { return state_.load(); }

    std::string GetLatestVersion() {
        std::lock_guard<std::mutex> lock(mutex_);
        return latestVersion_;
    }

    bool IsManualCheck() const { return manualCheck_; }

    // Popup control
    bool ShouldShowPopup() const { return showPopup_; }
    void PopupShown() { showPopup_ = false; }

    void OpenReleasePage() {
        ShellExecuteW(nullptr, L"open",
            L"https://github.com/Misaka10571/joycon2-connector/releases/latest",
            nullptr, nullptr, SW_SHOWNORMAL);
    }

private:
    UpdateChecker() = default;

    std::atomic<UpdateState> state_{ UpdateState::Idle };
    std::mutex mutex_;
    std::string latestVersion_;
    std::atomic<bool> showPopup_{ false };
    std::atomic<bool> manualCheck_{ false };

    // Extract "tag_name" value from GitHub API JSON response
    static std::string ExtractTagName(const std::string& json) {
        const std::string key = "\"tag_name\"";
        auto pos = json.find(key);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        // Find opening quote of value
        auto start = json.find('"', pos + 1);
        if (start == std::string::npos) return "";
        auto end = json.find('"', start + 1);
        if (end == std::string::npos) return "";
        return json.substr(start + 1, end - start - 1);
    }

    // Parse "MAJOR.MINOR.PATCH" into three integers
    static bool ParseVersion(const std::string& ver, int& major, int& minor, int& patch) {
        major = minor = patch = 0;
        std::istringstream iss(ver);
        char dot1, dot2;
        if (!(iss >> major >> dot1 >> minor >> dot2 >> patch)) {
            // Try parsing with fewer components
            std::istringstream iss2(ver);
            if (!(iss2 >> major >> dot1 >> minor)) {
                return false;
            }
            patch = 0;
        }
        return true;
    }

    // Returns true if remoteVer is newer than current APP_VERSION
    static bool IsNewerVersion(const std::string& remoteVer) {
        int rMajor, rMinor, rPatch;
        if (!ParseVersion(remoteVer, rMajor, rMinor, rPatch)) return false;

        if (rMajor != APP_VERSION_MAJOR) return rMajor > APP_VERSION_MAJOR;
        if (rMinor != APP_VERSION_MINOR) return rMinor > APP_VERSION_MINOR;
        return rPatch > APP_VERSION_PATCH;
    }
};

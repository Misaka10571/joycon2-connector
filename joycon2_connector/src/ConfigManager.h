#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <map>
#include <filesystem>
#include <system_error>
#include <Windows.h>
#include <shlobj.h>
#include "Logger.h"

// GL/GR Button Mapping Configuration
enum class ButtonMapping {
    NONE,
    L3, R3,
    L1, R1,
    L2, R2,
    CROSS, CIRCLE, SQUARE, TRIANGLE,
    SHARE, OPTIONS,
    DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT
};

struct GLGRLayout {
    std::string name;
    ButtonMapping glMapping = ButtonMapping::NONE;
    ButtonMapping grMapping = ButtonMapping::NONE;
};

struct ProControllerConfig {
    std::vector<GLGRLayout> layouts;
    int activeLayoutIndex = 0;
};

struct MouseConfig {
    bool chatKeyEnabled = true;
    float fastSensitivity = 1.0f;
    float normalSensitivity = 0.6f;
    float slowSensitivity = 0.3f;
    float scrollSpeed = 40.0f;
    bool interpolationEnabled = true;
    int interpolationRateHz = 125;
};

struct VibrationConfig {
    bool enabled = true;
    float intensity = 1.0f;    // 0.0 - 1.0 scale factor
};

// Per-device settings (keyed by BLE address)
struct DeviceSettings {
    bool swapABXY = false;  // Swap A⇄B / X⇄Y button positions
    bool useRawVibration = true;  // true = raw motor control (0x5N), false = predefined samples (0x0A)
    bool useXboxEmulation = false;  // true = emulate Xbox 360 controller instead of DS4
};

struct AppConfig {
    ProControllerConfig proConfig;
    MouseConfig mouseConfig;
    VibrationConfig vibrationConfig;
    std::string language;  // "en", "zh", or "" (auto-detect)
    std::map<uint64_t, DeviceSettings> deviceSettings;  // per-device settings, keyed by BLE address
    float gyroSensitivity = 1.0f;  // DS4 gyro multiplier (1.0 = raw JoyCon2 values)
    bool minimizeToTray = false;  // Minimize to system tray on close instead of exiting
    bool autoCheckUpdate = false;  // Auto check for updates on startup (default off)
    bool suppressXboxWarning = false;  // Don't show Xbox emulation gyro warning
    bool nonWindows11WarningShown = false;  // Only show the Bluetooth rate warning once
};

// Button mapping string conversion helpers
inline std::string ButtonMappingToString(ButtonMapping mapping) {
    switch (mapping) {
    case ButtonMapping::NONE:       return "NONE";
    case ButtonMapping::L3:         return "L3";
    case ButtonMapping::R3:         return "R3";
    case ButtonMapping::L1:         return "L1";
    case ButtonMapping::R1:         return "R1";
    case ButtonMapping::L2:         return "L2";
    case ButtonMapping::R2:         return "R2";
    case ButtonMapping::CROSS:      return "CROSS";
    case ButtonMapping::CIRCLE:     return "CIRCLE";
    case ButtonMapping::SQUARE:     return "SQUARE";
    case ButtonMapping::TRIANGLE:   return "TRIANGLE";
    case ButtonMapping::SHARE:      return "SHARE";
    case ButtonMapping::OPTIONS:    return "OPTIONS";
    case ButtonMapping::DPAD_UP:    return "DPAD_UP";
    case ButtonMapping::DPAD_DOWN:  return "DPAD_DOWN";
    case ButtonMapping::DPAD_LEFT:  return "DPAD_LEFT";
    case ButtonMapping::DPAD_RIGHT: return "DPAD_RIGHT";
    default: return "NONE";
    }
}

inline ButtonMapping StringToButtonMapping(const std::string& str) {
    static const std::map<std::string, ButtonMapping> m = {
        {"NONE", ButtonMapping::NONE}, {"L3", ButtonMapping::L3}, {"R3", ButtonMapping::R3},
        {"L1", ButtonMapping::L1}, {"R1", ButtonMapping::R1}, {"L2", ButtonMapping::L2}, {"R2", ButtonMapping::R2},
        {"CROSS", ButtonMapping::CROSS}, {"CIRCLE", ButtonMapping::CIRCLE},
        {"SQUARE", ButtonMapping::SQUARE}, {"TRIANGLE", ButtonMapping::TRIANGLE},
        {"SHARE", ButtonMapping::SHARE}, {"OPTIONS", ButtonMapping::OPTIONS},
        {"DPAD_UP", ButtonMapping::DPAD_UP}, {"DPAD_DOWN", ButtonMapping::DPAD_DOWN},
        {"DPAD_LEFT", ButtonMapping::DPAD_LEFT}, {"DPAD_RIGHT", ButtonMapping::DPAD_RIGHT}
    };
    auto it = m.find(str);
    return (it != m.end()) ? it->second : ButtonMapping::NONE;
}

inline const char* ButtonMappingNames[] = {
    "NONE", "L3", "R3", "L1", "R1", "L2", "R2",
    "CROSS", "CIRCLE", "SQUARE", "TRIANGLE",
    "SHARE", "OPTIONS",
    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT"
};
inline const char* ButtonMappingXboxNames[] = {
    "NONE", "LS", "RS", "LB", "RB", "LT", "RT",
    "A", "B", "X", "Y",
    "BACK", "START",
    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT"
};
inline constexpr int ButtonMappingCount = 17;

inline const char* ButtonMappingDisplayName(ButtonMapping mapping, bool xboxMode) {
    int index = static_cast<int>(mapping);
    if (index < 0 || index >= ButtonMappingCount) index = 0;
    return xboxMode ? ButtonMappingXboxNames[index] : ButtonMappingNames[index];
}

// Simple JSON serialization
inline std::string ConfigToJSON(const AppConfig& config) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"activeLayoutIndex\": " << config.proConfig.activeLayoutIndex << ",\n";
    oss << "  \"layouts\": [\n";
    for (size_t i = 0; i < config.proConfig.layouts.size(); ++i) {
        const auto& l = config.proConfig.layouts[i];
        oss << "    { \"name\": \"" << l.name
            << "\", \"gl\": \"" << ButtonMappingToString(l.glMapping)
            << "\", \"gr\": \"" << ButtonMappingToString(l.grMapping) << "\" }";
        if (i + 1 < config.proConfig.layouts.size()) oss << ",";
        oss << "\n";
    }
    oss << "  ],\n";
    oss << "  \"mouse\": {\n";
    oss << "    \"chatKeyEnabled\": " << (config.mouseConfig.chatKeyEnabled ? "true" : "false") << ",\n";
    oss << "    \"fastSensitivity\": " << config.mouseConfig.fastSensitivity << ",\n";
    oss << "    \"normalSensitivity\": " << config.mouseConfig.normalSensitivity << ",\n";
    oss << "    \"slowSensitivity\": " << config.mouseConfig.slowSensitivity << ",\n";
    oss << "    \"scrollSpeed\": " << config.mouseConfig.scrollSpeed << ",\n";
    oss << "    \"interpolationEnabled\": " << (config.mouseConfig.interpolationEnabled ? "true" : "false") << ",\n";
    oss << "    \"interpolationRateHz\": " << config.mouseConfig.interpolationRateHz << "\n";
    oss << "  },\n";
    oss << "  \"vibration\": {\n";
    oss << "    \"enabled\": " << (config.vibrationConfig.enabled ? "true" : "false") << ",\n";
    oss << "    \"intensity\": " << config.vibrationConfig.intensity << "\n";
    oss << "  },\n";
    oss << "  \"gyroSensitivity\": " << config.gyroSensitivity << ",\n";
    oss << "  \"language\": \"" << config.language << "\",\n";
    oss << "  \"minimizeToTray\": " << (config.minimizeToTray ? "true" : "false") << ",\n";
    oss << "  \"autoCheckUpdate\": " << (config.autoCheckUpdate ? "true" : "false") << ",\n";
    oss << "  \"suppressXboxWarning\": " << (config.suppressXboxWarning ? "true" : "false") << ",\n";
    oss << "  \"nonWindows11WarningShown\": " << (config.nonWindows11WarningShown ? "true" : "false") << ",\n";
    oss << "  \"deviceSettings\": [\n";
    size_t dsIdx = 0;
    for (const auto& [addr, ds] : config.deviceSettings) {
        oss << "    { \"addr\": \"" << addr << "\", \"swapABXY\": " << (ds.swapABXY ? "true" : "false")
            << ", \"useRawVibration\": " << (ds.useRawVibration ? "true" : "false")
            << ", \"useXboxEmulation\": " << (ds.useXboxEmulation ? "true" : "false") << " }";
        if (dsIdx + 1 < config.deviceSettings.size()) oss << ",";
        oss << "\n";
        dsIdx++;
    }
    oss << "  ]\n";
    oss << "}";
    return oss.str();
}

// Helper to extract a JSON string value
inline std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    auto pos = json.find(searchKey);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    auto start = json.find('"', pos + 1);
    if (start == std::string::npos) return "";
    auto end = json.find('"', start + 1);
    if (end == std::string::npos) return "";
    return json.substr(start + 1, end - start - 1);
}

// Helper to extract a JSON number value
inline double ExtractJsonNumber(const std::string& json, const std::string& key, double defaultVal = 0.0) {
    std::string searchKey = "\"" + key + "\"";
    auto pos = json.find(searchKey);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return defaultVal;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    try { return std::stod(json.substr(pos)); } catch (...) { return defaultVal; }
}

// Helper to extract a JSON bool value
inline bool ExtractJsonBool(const std::string& json, const std::string& key, bool defaultVal = false) {
    std::string searchKey = "\"" + key + "\"";
    auto pos = json.find(searchKey);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return defaultVal;
    auto rest = json.substr(pos + 1);
    if (rest.find("true") < rest.find("false")) return true;
    return false;
}

inline bool JSONToConfig(const std::string& json, AppConfig& config) {
    // Parse activeLayoutIndex
    config.proConfig.activeLayoutIndex = static_cast<int>(ExtractJsonNumber(json, "activeLayoutIndex", 0));

    // Parse layouts array
    config.proConfig.layouts.clear();
    auto layoutsPos = json.find("\"layouts\"");
    if (layoutsPos != std::string::npos) {
        auto arrStart = json.find('[', layoutsPos);
        auto arrEnd = json.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::string arrStr = json.substr(arrStart, arrEnd - arrStart + 1);
            size_t objPos = 0;
            while ((objPos = arrStr.find('{', objPos)) != std::string::npos) {
                auto objEnd = arrStr.find('}', objPos);
                if (objEnd == std::string::npos) break;
                std::string objStr = arrStr.substr(objPos, objEnd - objPos + 1);
                
                GLGRLayout layout;
                layout.name = ExtractJsonString(objStr, "name");
                layout.glMapping = StringToButtonMapping(ExtractJsonString(objStr, "gl"));
                layout.grMapping = StringToButtonMapping(ExtractJsonString(objStr, "gr"));
                config.proConfig.layouts.push_back(layout);
                objPos = objEnd + 1;
            }
        }
    }

    // Parse mouse config
    auto mousePos = json.find("\"mouse\"");
    if (mousePos != std::string::npos) {
        auto mouseStart = json.find('{', mousePos);
        auto mouseEnd = json.find('}', mouseStart);
        if (mouseStart != std::string::npos && mouseEnd != std::string::npos) {
            std::string mouseStr = json.substr(mouseStart, mouseEnd - mouseStart + 1);
            config.mouseConfig.chatKeyEnabled = ExtractJsonBool(mouseStr, "chatKeyEnabled", true);
            config.mouseConfig.fastSensitivity = (float)ExtractJsonNumber(mouseStr, "fastSensitivity", 1.0);
            config.mouseConfig.normalSensitivity = (float)ExtractJsonNumber(mouseStr, "normalSensitivity", 0.6);
            config.mouseConfig.slowSensitivity = (float)ExtractJsonNumber(mouseStr, "slowSensitivity", 0.3);
            config.mouseConfig.scrollSpeed = (float)ExtractJsonNumber(mouseStr, "scrollSpeed", 40.0);
            config.mouseConfig.interpolationEnabled = ExtractJsonBool(mouseStr, "interpolationEnabled", true);
            config.mouseConfig.interpolationRateHz = static_cast<int>(ExtractJsonNumber(mouseStr, "interpolationRateHz", 500));
        }
    }

    // Parse vibration config
    auto vibPos = json.find("\"vibration\"");
    if (vibPos != std::string::npos) {
        auto vibStart = json.find('{', vibPos);
        auto vibEnd = json.find('}', vibStart);
        if (vibStart != std::string::npos && vibEnd != std::string::npos) {
            std::string vibStr = json.substr(vibStart, vibEnd - vibStart + 1);
            config.vibrationConfig.enabled = ExtractJsonBool(vibStr, "enabled", true);
            config.vibrationConfig.intensity = (float)ExtractJsonNumber(vibStr, "intensity", 1.0);
        }
    }

    // Parse gyro sensitivity (default 1.0 preserves the original raw values)
    config.gyroSensitivity = (float)ExtractJsonNumber(json, "gyroSensitivity", 1.0);
    if (config.gyroSensitivity <= 0.0f || config.gyroSensitivity > 8.0f)
        config.gyroSensitivity = 1.0f;

    // Parse language (with backward compatibility for old short codes)
    config.language = ExtractJsonString(json, "language");
    if (config.language == "en") config.language = "en_us";
    else if (config.language == "zh") config.language = "zh_cn";

    // Parse minimizeToTray
    config.minimizeToTray = ExtractJsonBool(json, "minimizeToTray", false);

    // Parse autoCheckUpdate
    config.autoCheckUpdate = ExtractJsonBool(json, "autoCheckUpdate", false);

    // Parse suppressXboxWarning
    config.suppressXboxWarning = ExtractJsonBool(json, "suppressXboxWarning", false);

    // Parse one-time Windows version warning state
    config.nonWindows11WarningShown = ExtractJsonBool(json, "nonWindows11WarningShown", false);

    // Parse per-device settings
    config.deviceSettings.clear();
    auto dsPos = json.find("\"deviceSettings\"");
    if (dsPos != std::string::npos) {
        auto dsArrStart = json.find('[', dsPos);
        auto dsArrEnd = json.find(']', dsArrStart);
        if (dsArrStart != std::string::npos && dsArrEnd != std::string::npos) {
            std::string dsArrStr = json.substr(dsArrStart, dsArrEnd - dsArrStart + 1);
            size_t dsObjPos = 0;
            while ((dsObjPos = dsArrStr.find('{', dsObjPos)) != std::string::npos) {
                auto dsObjEnd = dsArrStr.find('}', dsObjPos);
                if (dsObjEnd == std::string::npos) break;
                std::string dsObjStr = dsArrStr.substr(dsObjPos, dsObjEnd - dsObjPos + 1);
                std::string addrStr = ExtractJsonString(dsObjStr, "addr");
                if (!addrStr.empty()) {
                    try {
                        uint64_t addr = std::stoull(addrStr);
                        DeviceSettings ds;
                        ds.swapABXY = ExtractJsonBool(dsObjStr, "swapABXY", false);
                        ds.useRawVibration = ExtractJsonBool(dsObjStr, "useRawVibration", true);
                        ds.useXboxEmulation = ExtractJsonBool(dsObjStr, "useXboxEmulation", false);
                        config.deviceSettings[addr] = ds;
                    } catch (...) {}
                }
                dsObjPos = dsObjEnd + 1;
            }
        }
    }

    return true;
}

class ConfigManager {
public:
    static ConfigManager& Instance() {
        static ConfigManager inst;
        return inst;
    }

    AppConfig config;
    const std::filesystem::path configFile;

    bool Load() {
        MigrateLegacyConfig();
        std::ifstream file(configFile);
        if (!file.is_open()) {
            APP_LOG_DEBUG("Config file %s not found (first launch?)", configFile.string().c_str());
            return false;
        }
        std::stringstream ss;
        ss << file.rdbuf();
        APP_LOG_DEBUG("Config loaded from %s", configFile.string().c_str());
        return JSONToConfig(ss.str(), config);
    }

    void Save() {
        std::error_code ec;
        const auto configDirectory = configFile.parent_path();
        if (!configDirectory.empty()) {
            std::filesystem::create_directories(configDirectory, ec);
            if (ec) {
                APP_LOG_WARNING("Could not create config directory %s (error %d)",
                    configDirectory.string().c_str(), ec.value());
                return;
            }
        }

        std::ofstream file(configFile);
        if (file.is_open()) {
            file << ConfigToJSON(config);
            file.close();
            APP_LOG_DEBUG("Config saved to %s", configFile.string().c_str());
        } else {
            APP_LOG_WARNING("Could not open %s for writing", configFile.string().c_str());
        }
    }

    void EnsureDefaults() {
        if (config.proConfig.layouts.empty()) {
            GLGRLayout defaultLayout;
            defaultLayout.name = "Layout 1";
            defaultLayout.glMapping = ButtonMapping::NONE;
            defaultLayout.grMapping = ButtonMapping::NONE;
            config.proConfig.layouts.push_back(defaultLayout);
            config.proConfig.activeLayoutIndex = 0;
        }
    }

    // Get per-device settings (creates default entry if not found)
    DeviceSettings& GetDeviceSettings(uint64_t bleAddr) {
        return config.deviceSettings[bleAddr];
    }

    // Update per-device settings and persist
    void SaveDeviceSettings(uint64_t bleAddr, const DeviceSettings& settings) {
        config.deviceSettings[bleAddr] = settings;
        Save();
    }

private:
    ConfigManager() : configFile(GetConfigFilePath()) {}

    static std::filesystem::path GetConfigFilePath() {
        PWSTR localAppData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData))) {
            std::filesystem::path path(localAppData);
            CoTaskMemFree(localAppData);
            return path / L"joycon2_connector" / L"joycon2_config.json";
        }

        wchar_t fallback[MAX_PATH] = {};
        DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", fallback, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            return std::filesystem::path(fallback) / L"joycon2_connector" / L"joycon2_config.json";
        }

        APP_LOG_WARNING("Could not locate Local AppData; falling back to the current directory");
        return std::filesystem::path(L"joycon2_config.json");
    }

    static std::filesystem::path GetExecutableDirectory() {
        std::wstring path(MAX_PATH, L'\0');
        for (;;) {
            DWORD copied = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (copied == 0) return {};
            if (copied < path.size()) {
                path.resize(copied);
                return std::filesystem::path(path).parent_path();
            }
            if (path.size() >= 32768) return {};
            path.resize(path.size() * 2);
        }
    }

    void MigrateLegacyConfig() {
        std::error_code ec;
        if (std::filesystem::exists(configFile, ec)) return;

        std::vector<std::filesystem::path> legacyFiles;
        auto currentDirectory = std::filesystem::current_path(ec);
        if (!ec) legacyFiles.push_back(currentDirectory / L"joycon2_config.json");
        auto executableDirectory = GetExecutableDirectory();
        if (!executableDirectory.empty()) {
            legacyFiles.push_back(executableDirectory / L"joycon2_config.json");
        }

        for (const auto& legacyFile : legacyFiles) {
            if (legacyFile.lexically_normal() == configFile.lexically_normal()) continue;

            ec.clear();
            if (!std::filesystem::is_regular_file(legacyFile, ec) || ec) continue;

            const auto configDirectory = configFile.parent_path();
            if (!configDirectory.empty()) {
                std::filesystem::create_directories(configDirectory, ec);
                if (ec) {
                    APP_LOG_WARNING("Could not create config directory for migration (error %d)", ec.value());
                    return;
                }
            }

            std::filesystem::copy_file(legacyFile, configFile, std::filesystem::copy_options::none, ec);
            if (ec) {
                APP_LOG_WARNING("Could not migrate config from %s to %s (error %d)",
                    legacyFile.string().c_str(), configFile.string().c_str(), ec.value());
                return;
            }

            std::error_code removeError;
            std::filesystem::remove(legacyFile, removeError);
            if (removeError) {
                APP_LOG_WARNING("Config migrated, but old file %s could not be removed (error %d)",
                    legacyFile.string().c_str(), removeError.value());
            } else {
                APP_LOG_INFO("Config migrated from %s to %s",
                    legacyFile.string().c_str(), configFile.string().c_str());
            }
            return;
        }
    }
};

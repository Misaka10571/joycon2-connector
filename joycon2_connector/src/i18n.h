#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <Windows.h>
#include "lang_data.h"

// Information about an available language
struct LangInfo {
    std::string locale;       // e.g. "zh_cn", "en_us"
    std::string displayName;  // e.g. "中文", "English"
    std::string jsonData;     // full JSON content (embedded)
};

// Internationalization manager — loads translations from embedded JSON data
class I18nManager {
public:
    static I18nManager& Instance() {
        static I18nManager inst;
        return inst;
    }

    // Initialize from embedded language data (compiled into the binary)
    void InitFromEmbedded() {
        availableLanguages.clear();
        const auto& embedded = GetEmbeddedLanguages();
        for (const auto& e : embedded) {
            std::string content(e.jsonData);
            LangInfo info;
            info.jsonData = content;
            info.locale = ExtractJsonValue(content, "_locale");
            info.displayName = ExtractJsonValue(content, "_display_name");
            if (!info.locale.empty() && !info.displayName.empty()) {
                availableLanguages.push_back(std::move(info));
            }
        }
        // Sort by locale for stable ordering
        std::sort(availableLanguages.begin(), availableLanguages.end(),
            [](const LangInfo& a, const LangInfo& b) { return a.locale < b.locale; });
    }

    // Load a specific language by locale code (e.g. "zh_cn")
    bool LoadLanguage(const std::string& locale) {
        for (const auto& lang : availableLanguages) {
            if (lang.locale == locale) {
                translations.clear();
                ParseJsonToMap(lang.jsonData, translations);
                currentLocale = locale;
                return true;
            }
        }
        return false;
    }

    // Translate a key; returns key itself if not found
    const char* Translate(const char* key) {
        auto it = translations.find(key);
        if (it != translations.end()) {
            return it->second.c_str();
        }
        return key;
    }

    const std::vector<LangInfo>& GetAvailableLanguages() const {
        return availableLanguages;
    }

    const std::string& GetCurrentLocale() const {
        return currentLocale;
    }

    // Find index of a locale in availableLanguages, -1 if not found
    int GetLocaleIndex(const std::string& locale) const {
        for (int i = 0; i < (int)availableLanguages.size(); ++i) {
            if (availableLanguages[i].locale == locale) return i;
        }
        return -1;
    }

private:
    I18nManager() = default;

    std::vector<LangInfo> availableLanguages;
    std::unordered_map<std::string, std::string> translations;
    std::string currentLocale;

    // Extract a string value for a given key from JSON content
    // Handles simple "key": "value" pairs (no nested objects)
    static std::string ExtractJsonValue(const std::string& json, const std::string& key) {
        std::string searchKey = "\"" + key + "\"";
        auto pos = json.find(searchKey);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos + searchKey.size());
        if (pos == std::string::npos) return "";
        pos++; // skip ':'
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n'))
            pos++;
        if (pos >= json.size() || json[pos] != '"') return "";
        pos++; // skip opening '"'
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                char next = json[pos + 1];
                if (next == '"') { result += '"'; pos += 2; continue; }
                if (next == '\\') { result += '\\'; pos += 2; continue; }
                if (next == 'n') { result += '\n'; pos += 2; continue; }
                if (next == 't') { result += '\t'; pos += 2; continue; }
                if (next == '/') { result += '/'; pos += 2; continue; }
                if (next == 'u') {
                    // Parse \uXXXX unicode escape
                    if (pos + 5 < json.size()) {
                        std::string hexStr = json.substr(pos + 2, 4);
                        unsigned int codepoint = 0;
                        try { codepoint = std::stoul(hexStr, nullptr, 16); } catch (...) {}
                        if (codepoint > 0) {
                            // Encode as UTF-8
                            if (codepoint <= 0x7F) {
                                result += (char)codepoint;
                            } else if (codepoint <= 0x7FF) {
                                result += (char)(0xC0 | (codepoint >> 6));
                                result += (char)(0x80 | (codepoint & 0x3F));
                            } else {
                                result += (char)(0xE0 | (codepoint >> 12));
                                result += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                                result += (char)(0x80 | (codepoint & 0x3F));
                            }
                        }
                        pos += 6;
                        continue;
                    }
                }
                // Unknown escape, keep as-is
                result += json[pos];
                pos++;
            } else {
                result += json[pos];
                pos++;
            }
        }
        return result;
    }

    // Parse all "key": "value" pairs from a flat JSON object into a map
    static void ParseJsonToMap(const std::string& json, std::unordered_map<std::string, std::string>& map) {
        size_t pos = 0;
        while (pos < json.size()) {
            // Find next key (opening quote)
            auto keyStart = json.find('"', pos);
            if (keyStart == std::string::npos) break;
            auto keyEnd = json.find('"', keyStart + 1);
            if (keyEnd == std::string::npos) break;
            std::string key = json.substr(keyStart + 1, keyEnd - keyStart - 1);

            // Find colon
            auto colonPos = json.find(':', keyEnd + 1);
            if (colonPos == std::string::npos) break;

            // Skip whitespace after colon
            size_t valPos = colonPos + 1;
            while (valPos < json.size() && (json[valPos] == ' ' || json[valPos] == '\t' || json[valPos] == '\r' || json[valPos] == '\n'))
                valPos++;

            if (valPos >= json.size() || json[valPos] != '"') {
                // Not a string value, skip past this line
                pos = valPos + 1;
                continue;
            }

            // Use ExtractJsonValue to properly handle escapes
            std::string value = ExtractJsonValue(json.substr(keyStart), key);
            if (!key.empty()) {
                map[key] = value;
            }

            // Move past the value string
            valPos++; // skip opening quote
            while (valPos < json.size()) {
                if (json[valPos] == '\\') { valPos += 2; continue; }
                if (json[valPos] == '"') { valPos++; break; }
                valPos++;
            }
            pos = valPos;
        }
    }
};

// Detect system UI language: returns locale string
inline std::string DetectSystemLanguage() {
    LANGID langId = GetUserDefaultUILanguage();
    WORD primaryLang = PRIMARYLANGID(langId);
    if (primaryLang == LANG_CHINESE) {
        return "zh_cn";
    }
    return "en_us";
}

// Global translation function — signature unchanged, all call sites work as before
inline const char* T(const char* key) {
    return I18nManager::Instance().Translate(key);
}

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#pragma comment(lib, "setupapi.lib")
#include <iostream>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <fstream>
#include <sstream>
#include <map>
#include <conio.h>  // For _kbhit()
#include "JoyConDecoder.h"
#include <Windows.h>

#include <ViGEm/Client.h>
#include <ViGEm/Common.h>

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;

constexpr uint16_t JOYCON_MANUFACTURER_ID = 1363; // Nintendo
const std::vector<uint8_t> JOYCON_MANUFACTURER_PREFIX = { 0x01, 0x00, 0x03, 0x7E };
const wchar_t* INPUT_REPORT_UUID = L"ab7de9be-89fe-49ad-828f-118f09df7fd2";
const wchar_t* WRITE_COMMAND_UUID = L"649d4ac9-8eb7-4e6c-af44-1ea54fe5f005";

// GL/GR Button Mapping Configuration
enum class ButtonMapping {
    NONE,
    L3,        // Left stick click
    R3,        // Right stick click
    L1,        // Left shoulder
    R1,        // Right shoulder
    L2,        // Left trigger
    R2,        // Right trigger
    CROSS,     // X / A
    CIRCLE,    // O / B
    SQUARE,    // □ / X
    TRIANGLE,  // △ / Y
    SHARE,     // Share / Back
    OPTIONS,   // Options / Start
    DPAD_UP,
    DPAD_DOWN,
    DPAD_LEFT,
    DPAD_RIGHT
};

// Single GL/GR mapping layout
struct GLGRLayout {
    std::string name;
    ButtonMapping glMapping;
    ButtonMapping grMapping;
};

// Pro Controller configuration with multiple layouts
struct ProControllerConfig {
    std::vector<GLGRLayout> layouts;
    int activeLayoutIndex = 0;
};

const std::string CONFIG_FILE = "joycon2cpp_config.json";

// Global config instance
ProControllerConfig g_proControllerConfig;

PVIGEM_CLIENT vigem_client = nullptr;

void InitializeViGEm()
{
    if (vigem_client != nullptr)
        return;

    vigem_client = vigem_alloc();
    if (vigem_client == nullptr)
    {
        std::wcerr << L"Failed to allocate ViGEm client.\n";
        exit(1);
    }

    auto ret = vigem_connect(vigem_client);
    if (!VIGEM_SUCCESS(ret))
    {
        std::wcerr << L"Failed to connect to ViGEm bus: 0x" << std::hex << ret << L"\n";
        exit(1);
    }

    std::wcout << L"ViGEm client initialized and connected.\n";
}

void PrintRawNotification(const std::vector<uint8_t>& buffer)
{
    std::cout << "[Raw Notification] ";
    for (auto b : buffer) {
        printf("%02X ", b);
    }
    std::cout << std::endl;
}

// Helper: Convert ButtonMapping to string
std::string ButtonMappingToString(ButtonMapping mapping) {
    switch (mapping) {
        case ButtonMapping::NONE: return "NONE";
        case ButtonMapping::L3: return "L3";
        case ButtonMapping::R3: return "R3";
        case ButtonMapping::L1: return "L1";
        case ButtonMapping::R1: return "R1";
        case ButtonMapping::L2: return "L2";
        case ButtonMapping::R2: return "R2";
        case ButtonMapping::CROSS: return "CROSS";
        case ButtonMapping::CIRCLE: return "CIRCLE";
        case ButtonMapping::SQUARE: return "SQUARE";
        case ButtonMapping::TRIANGLE: return "TRIANGLE";
        case ButtonMapping::SHARE: return "SHARE";
        case ButtonMapping::OPTIONS: return "OPTIONS";
        case ButtonMapping::DPAD_UP: return "DPAD_UP";
        case ButtonMapping::DPAD_DOWN: return "DPAD_DOWN";
        case ButtonMapping::DPAD_LEFT: return "DPAD_LEFT";
        case ButtonMapping::DPAD_RIGHT: return "DPAD_RIGHT";
        default: return "NONE";
    }
}

// Helper: Convert string to ButtonMapping
ButtonMapping StringToButtonMapping(const std::string& str) {
    if (str == "L3") return ButtonMapping::L3;
    if (str == "R3") return ButtonMapping::R3;
    if (str == "L1") return ButtonMapping::L1;
    if (str == "R1") return ButtonMapping::R1;
    if (str == "L2") return ButtonMapping::L2;
    if (str == "R2") return ButtonMapping::R2;
    if (str == "CROSS") return ButtonMapping::CROSS;
    if (str == "CIRCLE") return ButtonMapping::CIRCLE;
    if (str == "SQUARE") return ButtonMapping::SQUARE;
    if (str == "TRIANGLE") return ButtonMapping::TRIANGLE;
    if (str == "SHARE") return ButtonMapping::SHARE;
    if (str == "OPTIONS") return ButtonMapping::OPTIONS;
    if (str == "DPAD_UP") return ButtonMapping::DPAD_UP;
    if (str == "DPAD_DOWN") return ButtonMapping::DPAD_DOWN;
    if (str == "DPAD_LEFT") return ButtonMapping::DPAD_LEFT;
    if (str == "DPAD_RIGHT") return ButtonMapping::DPAD_RIGHT;
    return ButtonMapping::NONE;
}

// Simple JSON serialization for our config
std::string ConfigToJSON(const ProControllerConfig& config) {
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"activeLayoutIndex\": " << config.activeLayoutIndex << ",\n";
    ss << "  \"layouts\": [\n";

    for (size_t i = 0; i < config.layouts.size(); ++i) {
        const auto& layout = config.layouts[i];
        ss << "    {\n";
        ss << "      \"name\": \"" << layout.name << "\",\n";
        ss << "      \"glMapping\": \"" << ButtonMappingToString(layout.glMapping) << "\",\n";
        ss << "      \"grMapping\": \"" << ButtonMappingToString(layout.grMapping) << "\"\n";
        ss << "    }";
        if (i < config.layouts.size() - 1) ss << ",";
        ss << "\n";
    }

    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

// Simple JSON parsing for our config
bool JSONToConfig(const std::string& json, ProControllerConfig& config) {
    config.layouts.clear();
    config.activeLayoutIndex = 0;

    // Simple parser for our specific JSON structure
    size_t pos = 0;

    // Find activeLayoutIndex
    size_t activePos = json.find("\"activeLayoutIndex\"");
    if (activePos != std::string::npos) {
        size_t colonPos = json.find(':', activePos);
        size_t commaPos = json.find_first_of(",\n", colonPos);
        std::string value = json.substr(colonPos + 1, commaPos - colonPos - 1);
        // Trim whitespace
        value.erase(0, value.find_first_not_of(" \t\n\r"));
        value.erase(value.find_last_not_of(" \t\n\r") + 1);
        config.activeLayoutIndex = std::stoi(value);
    }

    // Find layouts array
    size_t layoutsStart = json.find("\"layouts\"");
    if (layoutsStart == std::string::npos) return false;

    size_t arrayStart = json.find('[', layoutsStart);
    if (arrayStart == std::string::npos) return false;

    // Parse each layout object
    pos = arrayStart + 1;
    while (pos < json.length()) {
        size_t objectStart = json.find('{', pos);
        if (objectStart == std::string::npos) break;

        size_t objectEnd = json.find('}', objectStart);
        if (objectEnd == std::string::npos) break;

        std::string objectStr = json.substr(objectStart, objectEnd - objectStart + 1);

        GLGRLayout layout;

        // Parse name
        size_t namePos = objectStr.find("\"name\"");
        if (namePos != std::string::npos) {
            size_t nameStart = objectStr.find('\"', namePos + 6);
            size_t nameEnd = objectStr.find('\"', nameStart + 1);
            layout.name = objectStr.substr(nameStart + 1, nameEnd - nameStart - 1);
        }

        // Parse glMapping
        size_t glPos = objectStr.find("\"glMapping\"");
        if (glPos != std::string::npos) {
            size_t glStart = objectStr.find('\"', glPos + 11);
            size_t glEnd = objectStr.find('\"', glStart + 1);
            std::string glStr = objectStr.substr(glStart + 1, glEnd - glStart - 1);
            layout.glMapping = StringToButtonMapping(glStr);
        }

        // Parse grMapping
        size_t grPos = objectStr.find("\"grMapping\"");
        if (grPos != std::string::npos) {
            size_t grStart = objectStr.find('\"', grPos + 11);
            size_t grEnd = objectStr.find('\"', grStart + 1);
            std::string grStr = objectStr.substr(grStart + 1, grEnd - grStart - 1);
            layout.grMapping = StringToButtonMapping(grStr);
        }

        config.layouts.push_back(layout);

        pos = objectEnd + 1;
        // Check if there's another object
        size_t nextComma = json.find(',', pos);
        size_t arrayEnd = json.find(']', pos);
        if (arrayEnd != std::string::npos && (nextComma == std::string::npos || arrayEnd < nextComma)) {
            break;
        }
    }

    return !config.layouts.empty();
}

// Load config from JSON file
bool LoadProControllerConfig(ProControllerConfig& config) {
    std::ifstream file(CONFIG_FILE);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    return JSONToConfig(buffer.str(), config);
}

// Save config to JSON file
void SaveProControllerConfig(const ProControllerConfig& config) {
    std::ofstream file(CONFIG_FILE);
    if (!file.is_open()) {
        std::cerr << "Failed to save config file.\n";
        return;
    }

    file << ConfigToJSON(config);
    file.close();
    std::cout << "Configuration saved to " << CONFIG_FILE << "\n";
}

// Prompt user for button mapping
ButtonMapping PromptForButtonMapping(const std::string& buttonName) {
    std::cout << "\nSelect mapping for " << buttonName << " button:\n";
    std::cout << "  1. L3 (Left Stick Click)\n";
    std::cout << "  2. R3 (Right Stick Click)\n";
    std::cout << "  3. L1 (Left Shoulder)\n";
    std::cout << "  4. R1 (Right Shoulder)\n";
    std::cout << "  5. L2 (Left Trigger)\n";
    std::cout << "  6. R2 (Right Trigger)\n";
    std::cout << "  7. Cross (X/A)\n";
    std::cout << "  8. Circle (O/B)\n";
    std::cout << "  9. Square (□/X)\n";
    std::cout << " 10. Triangle (△/Y)\n";
    std::cout << " 11. Share (Back)\n";
    std::cout << " 12. Options (Start)\n";
    std::cout << " 13. D-Pad Up\n";
    std::cout << " 14. D-Pad Down\n";
    std::cout << " 15. D-Pad Left\n";
    std::cout << " 16. D-Pad Right\n";
    std::cout << " 17. None (Disable)\n";
    std::cout << "Enter choice (1-17): ";

    int choice;
    std::cin >> choice;
    std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');

    switch (choice) {
        case 1: return ButtonMapping::L3;
        case 2: return ButtonMapping::R3;
        case 3: return ButtonMapping::L1;
        case 4: return ButtonMapping::R1;
        case 5: return ButtonMapping::L2;
        case 6: return ButtonMapping::R2;
        case 7: return ButtonMapping::CROSS;
        case 8: return ButtonMapping::CIRCLE;
        case 9: return ButtonMapping::SQUARE;
        case 10: return ButtonMapping::TRIANGLE;
        case 11: return ButtonMapping::SHARE;
        case 12: return ButtonMapping::OPTIONS;
        case 13: return ButtonMapping::DPAD_UP;
        case 14: return ButtonMapping::DPAD_DOWN;
        case 15: return ButtonMapping::DPAD_LEFT;
        case 16: return ButtonMapping::DPAD_RIGHT;
        case 17: return ButtonMapping::NONE;
        default:
            std::cout << "Invalid choice, defaulting to NONE.\n";
            return ButtonMapping::NONE;
    }
}

// Configure GL/GR mappings interactively
// Create default config with one empty layout
void CreateDefaultConfig() {
    g_proControllerConfig.layouts.clear();
    GLGRLayout defaultLayout;
    defaultLayout.name = "Layout 1";
    defaultLayout.glMapping = ButtonMapping::NONE;
    defaultLayout.grMapping = ButtonMapping::NONE;
    g_proControllerConfig.layouts.push_back(defaultLayout);
    g_proControllerConfig.activeLayoutIndex = 0;
    SaveProControllerConfig(g_proControllerConfig);
}

// Configure a single layout
void ConfigureLayout(GLGRLayout& layout) {
    std::cout << "\n=== Configure GL/GR Mapping ===\n";
    std::cout << "Layout Name: " << layout.name << "\n\n";

    layout.glMapping = PromptForButtonMapping("GL");
    layout.grMapping = PromptForButtonMapping("GR");

    std::cout << "\nLayout configured!\n";
    std::cout << "  GL -> " << ButtonMappingToString(layout.glMapping) << "\n";
    std::cout << "  GR -> " << ButtonMappingToString(layout.grMapping) << "\n";
}

// Layout management window
void ShowLayoutManagementWindow() {
    while (true) {
        std::cout << "\n==================================================\n";
        std::cout << "          GL/GR LAYOUT MANAGEMENT\n";
        std::cout << "==================================================\n";
        std::cout << "Current active layout: " << g_proControllerConfig.layouts[g_proControllerConfig.activeLayoutIndex].name << "\n\n";
        std::cout << "Available Layouts:\n";

        for (size_t i = 0; i < g_proControllerConfig.layouts.size(); ++i) {
            const auto& layout = g_proControllerConfig.layouts[i];
            std::cout << "  " << (i + 1) << ". " << layout.name;
            if (i == static_cast<size_t>(g_proControllerConfig.activeLayoutIndex)) {
                std::cout << " [ACTIVE]";
            }
            std::cout << "\n";
            std::cout << "     GL: " << ButtonMappingToString(layout.glMapping)
                      << " | GR: " << ButtonMappingToString(layout.grMapping) << "\n";
        }

        std::cout << "  " << (g_proControllerConfig.layouts.size() + 1) << ". [NEW]\n";
        std::cout << "  0. Exit Management Window\n\n";
        std::cout << "Enter number to edit layout: ";

        std::string input;
        std::getline(std::cin, input);

        if (input.empty()) continue;

        int choice = std::stoi(input);

        if (choice == 0) {
            SaveProControllerConfig(g_proControllerConfig);
            std::cout << "Exiting layout management...\n";
            break;
        }
        else if (choice == static_cast<int>(g_proControllerConfig.layouts.size() + 1)) {
            // Create new layout
            GLGRLayout newLayout;
            std::cout << "\nEnter name for new layout: ";
            std::getline(std::cin, newLayout.name);
            if (newLayout.name.empty()) {
                newLayout.name = "Layout " + std::to_string(g_proControllerConfig.layouts.size() + 1);
            }
            newLayout.glMapping = ButtonMapping::NONE;
            newLayout.grMapping = ButtonMapping::NONE;

            ConfigureLayout(newLayout);
            g_proControllerConfig.layouts.push_back(newLayout);
            SaveProControllerConfig(g_proControllerConfig);
            std::cout << "\nNew layout added!\n";
        }
        else if (choice > 0 && choice <= static_cast<int>(g_proControllerConfig.layouts.size())) {
            // Edit existing layout
            size_t index = choice - 1;
            std::cout << "\nEditing: " << g_proControllerConfig.layouts[index].name << "\n";
            std::cout << "1. Rename layout\n";
            std::cout << "2. Configure mappings\n";
            std::cout << "3. Delete layout\n";
            std::cout << "4. Set as active layout\n";
            std::cout << "0. Cancel\n";
            std::cout << "Choice: ";

            std::string editChoice;
            std::getline(std::cin, editChoice);

            if (editChoice == "1") {
                std::cout << "Enter new name: ";
                std::string newName;
                std::getline(std::cin, newName);
                if (!newName.empty()) {
                    g_proControllerConfig.layouts[index].name = newName;
                    SaveProControllerConfig(g_proControllerConfig);
                }
            }
            else if (editChoice == "2") {
                ConfigureLayout(g_proControllerConfig.layouts[index]);
                SaveProControllerConfig(g_proControllerConfig);
            }
            else if (editChoice == "3") {
                if (g_proControllerConfig.layouts.size() > 1) {
                    std::cout << "Are you sure you want to delete this layout? (y/n): ";
                    std::string confirm;
                    std::getline(std::cin, confirm);
                    if (confirm == "y" || confirm == "Y") {
                        g_proControllerConfig.layouts.erase(g_proControllerConfig.layouts.begin() + index);
                        if (g_proControllerConfig.activeLayoutIndex >= static_cast<int>(g_proControllerConfig.layouts.size())) {
                            g_proControllerConfig.activeLayoutIndex = static_cast<int>(g_proControllerConfig.layouts.size()) - 1;
                        }
                        SaveProControllerConfig(g_proControllerConfig);
                        std::cout << "Layout deleted!\n";
                    }
                } else {
                    std::cout << "Cannot delete the last layout!\n";
                }
            }
            else if (editChoice == "4") {
                g_proControllerConfig.activeLayoutIndex = static_cast<int>(index);
                SaveProControllerConfig(g_proControllerConfig);
                std::cout << "Active layout changed to: " << g_proControllerConfig.layouts[index].name << "\n";
            }
        }
    }
}

// Apply ButtonMapping to DS4 report
void ApplyButtonMapping(DS4_REPORT_EX& report, ButtonMapping mapping) {
    switch (mapping) {
        case ButtonMapping::L3:
            report.Report.wButtons |= DS4_BUTTON_THUMB_LEFT;
            break;
        case ButtonMapping::R3:
            report.Report.wButtons |= DS4_BUTTON_THUMB_RIGHT;
            break;
        case ButtonMapping::L1:
            report.Report.wButtons |= DS4_BUTTON_SHOULDER_LEFT;
            break;
        case ButtonMapping::R1:
            report.Report.wButtons |= DS4_BUTTON_SHOULDER_RIGHT;
            break;
        case ButtonMapping::L2:
            report.Report.bTriggerL = 255;
            break;
        case ButtonMapping::R2:
            report.Report.bTriggerR = 255;
            break;
        case ButtonMapping::CROSS:
            report.Report.wButtons |= DS4_BUTTON_CROSS;
            break;
        case ButtonMapping::CIRCLE:
            report.Report.wButtons |= DS4_BUTTON_CIRCLE;
            break;
        case ButtonMapping::SQUARE:
            report.Report.wButtons |= DS4_BUTTON_SQUARE;
            break;
        case ButtonMapping::TRIANGLE:
            report.Report.wButtons |= DS4_BUTTON_TRIANGLE;
            break;
        case ButtonMapping::SHARE:
            report.Report.wButtons |= DS4_BUTTON_SHARE;
            break;
        case ButtonMapping::OPTIONS:
            report.Report.wButtons |= DS4_BUTTON_OPTIONS;
            break;
        case ButtonMapping::DPAD_UP:
            DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&report.Report), DS4_BUTTON_DPAD_NORTH);
            break;
        case ButtonMapping::DPAD_DOWN:
            DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&report.Report), DS4_BUTTON_DPAD_SOUTH);
            break;
        case ButtonMapping::DPAD_LEFT:
            DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&report.Report), DS4_BUTTON_DPAD_WEST);
            break;
        case ButtonMapping::DPAD_RIGHT:
            DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&report.Report), DS4_BUTTON_DPAD_EAST);
            break;
        case ButtonMapping::NONE:
        default:
            // Do nothing
            break;
    }
}

// Send keyboard key press/release using Windows SendInput API
void SendKeyboardInput(WORD virtualKey, bool keyDown) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = keyDown ? 0 : KEYEVENTF_KEYUP;
    input.ki.time = 0;
    input.ki.dwExtraInfo = 0;

    SendInput(1, &input, sizeof(INPUT));
}

// Track button states to avoid repeated key presses
static bool g_screenshotButtonPressed = false;
static bool g_cButtonPressed = false;
static bool g_comboPressed = false;

// Flag for ZL+ZR+GL+GR combo to enter management window
static std::atomic<bool> g_openManagementWindow(false);

// Handle special Pro Controller buttons (Screenshot, C button, and combo detection)
void HandleSpecialProButtons(const std::vector<uint8_t>& buffer) {
    if (buffer.size() < 9) return;

    // Build button state from bytes 3-8
    uint64_t state = 0;
    for (int i = 3; i <= 8; ++i) {
        state = (state << 8) | buffer[i];
    }

    constexpr uint64_t BUTTON_SCREENSHOT_MASK = 0x000020000000;  // Bit 29
    constexpr uint64_t BUTTON_C_MASK = 0x000040000000;           // Bit 30
    constexpr uint64_t BUTTON_GL_MASK = 0x000000000200;          // Bit 9
    constexpr uint64_t BUTTON_GR_MASK = 0x000000000100;          // Bit 8
    constexpr uint64_t TRIGGER_ZL_MASK = 0x000000800000;         // ZL trigger
    constexpr uint64_t TRIGGER_ZR_MASK = 0x008000000000;         // ZR trigger

    // Check for ZL+ZR+GL+GR combo to open management window (only trigger on initial press)
    bool comboCurrentlyPressed = (state & TRIGGER_ZL_MASK) && (state & TRIGGER_ZR_MASK) &&
                                  (state & BUTTON_GL_MASK) && (state & BUTTON_GR_MASK);

    if (comboCurrentlyPressed && !g_comboPressed) {
        // Combo just pressed - trigger management window
        g_openManagementWindow.store(true);
        g_comboPressed = true;
    }
    else if (!comboCurrentlyPressed && g_comboPressed) {
        // Combo released - reset state
        g_comboPressed = false;
    }

    // Handle Screenshot button -> F12 key
    bool screenshotPressed = (state & BUTTON_SCREENSHOT_MASK) != 0;
    if (screenshotPressed && !g_screenshotButtonPressed) {
        // Button just pressed - send F12 key down
        SendKeyboardInput(VK_F12, true);
        g_screenshotButtonPressed = true;
    }
    else if (!screenshotPressed && g_screenshotButtonPressed) {
        // Button just released - send F12 key up
        SendKeyboardInput(VK_F12, false);
        g_screenshotButtonPressed = false;
    }

    // Handle C button -> Cycle through layouts
    bool cPressed = (state & BUTTON_C_MASK) != 0;
    if (cPressed && !g_cButtonPressed) {
        // Button just pressed - cycle to next layout
        if (!g_proControllerConfig.layouts.empty()) {
            int oldIndex = g_proControllerConfig.activeLayoutIndex;
            g_proControllerConfig.activeLayoutIndex = (g_proControllerConfig.activeLayoutIndex + 1) % g_proControllerConfig.layouts.size();
            SaveProControllerConfig(g_proControllerConfig);

            // Log layout change
            std::cout << "\n[Layout Changed] "
                      << g_proControllerConfig.layouts[oldIndex].name
                      << " -> "
                      << g_proControllerConfig.layouts[g_proControllerConfig.activeLayoutIndex].name
                      << " (GL: " << ButtonMappingToString(g_proControllerConfig.layouts[g_proControllerConfig.activeLayoutIndex].glMapping)
                      << ", GR: " << ButtonMappingToString(g_proControllerConfig.layouts[g_proControllerConfig.activeLayoutIndex].grMapping)
                      << ")\n";
        }
        g_cButtonPressed = true;
    }
    else if (!cPressed && g_cButtonPressed) {
        g_cButtonPressed = false;
    }
}

// Apply GL/GR mappings to Pro Controller report using active layout
void ApplyGLGRMappings(DS4_REPORT_EX& report, const std::vector<uint8_t>& buffer) {
    if (buffer.size() < 9) return;

    // Check if we have any layouts
    if (g_proControllerConfig.layouts.empty()) return;

    // Get the active layout
    int layoutIndex = g_proControllerConfig.activeLayoutIndex;
    if (layoutIndex < 0 || layoutIndex >= static_cast<int>(g_proControllerConfig.layouts.size())) {
        layoutIndex = 0;
        g_proControllerConfig.activeLayoutIndex = 0;
    }

    const GLGRLayout& activeLayout = g_proControllerConfig.layouts[layoutIndex];

    // Build button state from bytes 3-8 (same as in JoyConDecoder)
    uint64_t state = 0;
    for (int i = 3; i <= 8; ++i) {
        state = (state << 8) | buffer[i];
    }

    constexpr uint64_t BUTTON_GL_MASK = 0x000000000200;  // Bit 9
    constexpr uint64_t BUTTON_GR_MASK = 0x000000000100;  // Bit 8

    // Apply mappings if buttons are pressed
    if (state & BUTTON_GL_MASK) {
        ApplyButtonMapping(report, activeLayout.glMapping);
    }
    if (state & BUTTON_GR_MASK) {
        ApplyButtonMapping(report, activeLayout.grMapping);
    }
}

void SendCustomCommands(GattCharacteristic const& characteristic)
{
    std::vector<std::vector<uint8_t>> commands = {
        { 0x0c, 0x91, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 },
        { 0x0c, 0x91, 0x01, 0x04, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 }
    };

    for (const auto& cmd : commands)
    {
        auto writer = DataWriter();
        writer.WriteBytes(cmd);
        IBuffer buffer = writer.DetachBuffer();

        auto status = characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).get();

        if (status == GattCommunicationStatus::Success)
        {
            std::wcout << L"Command sent successfully.\n";
        }
        else
        {
            std::wcout << L"Failed to send command.\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
// Helper to send generic commands matching main.py structure
void SendGenericCommand(GattCharacteristic const& characteristic, uint8_t cmdId, uint8_t subCmdId, const std::vector<uint8_t>& data) {
    if (!characteristic) return;

    DataWriter writer;
    
    // Structure: CmdID, 0x91, 0x01, SubCmdID, 0x00, Len, 0x00, 0x00, Data...
    writer.WriteByte(cmdId);
    writer.WriteByte(0x91);
    writer.WriteByte(0x01);
    writer.WriteByte(subCmdId);
    writer.WriteByte(0x00);
    writer.WriteByte(static_cast<uint8_t>(data.size()));
    writer.WriteByte(0x00);
    writer.WriteByte(0x00);
    
    // Write Data
    for (uint8_t b : data) {
        writer.WriteByte(b);
    }

    IBuffer buffer = writer.DetachBuffer();
    characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).get();
    
    // Small delay to prevent flooding
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void EmitSound(GattCharacteristic const& characteristic) {
    // CMD 0x0A, SUB 0x02, Data: 0x04 (Preset) + Padding (up to 8 bytes total)
    std::vector<uint8_t> data(8, 0x00);
    data[0] = 0x04; // Preset ID
    SendGenericCommand(characteristic, 0x0A, 0x02, data);
}

void SetPlayerLEDs(GattCharacteristic const& characteristic, uint8_t pattern) {
    // CMD 0x09, SUB 0x07, Data: pattern + Padding (up to 8 bytes total)
    std::vector<uint8_t> data(8, 0x00);
    data[0] = pattern;
    SendGenericCommand(characteristic, 0x09, 0x07, data);
}

struct ConnectedJoyCon {
    BluetoothLEDevice device = nullptr;
    GattCharacteristic inputChar = nullptr;
    GattCharacteristic writeChar = nullptr;
};

ConnectedJoyCon WaitForJoyCon(const std::wstring& prompt)
{
    std::wcout << prompt << L"\n";

    ConnectedJoyCon cj{};

    BluetoothLEDevice device = nullptr;
    // Use atomic flag to avoid race: notify_one() can fire before wait_for() if
    // the JoyCon is already powered on when scanning starts.
    std::atomic<bool> connected{ false };

    BluetoothLEAdvertisementWatcher watcher;

    std::mutex mtx;
    std::condition_variable cv;

    watcher.Received([&](auto const&, auto const& args)
        {
            // Check flag first (lock-free fast path) to skip duplicate events
            if (connected.load(std::memory_order_acquire)) return;

            auto mfg = args.Advertisement().ManufacturerData();
            for (uint32_t i = 0; i < mfg.Size(); i++)
            {
                auto section = mfg.GetAt(i);
                if (section.CompanyId() != JOYCON_MANUFACTURER_ID) continue;
                auto reader = DataReader::FromBuffer(section.Data());
                std::vector<uint8_t> data(reader.UnconsumedBufferLength());
                reader.ReadBytes(data);
                if (data.size() >= JOYCON_MANUFACTURER_PREFIX.size() &&
                    std::equal(JOYCON_MANUFACTURER_PREFIX.begin(), JOYCON_MANUFACTURER_PREFIX.end(), data.begin()))
                {
                    // Atomically claim the connection slot
                    bool expected = false;
                    if (!connected.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                        return; // Another invocation of this callback already claimed it

                    BluetoothLEDevice dev = BluetoothLEDevice::FromBluetoothAddressAsync(args.BluetoothAddress()).get();
                    if (!dev) {
                        // Failed to open device — reset flag and let the scan continue
                        connected.store(false, std::memory_order_release);
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        device = dev;
                    }
                    // NOTE: Do NOT call watcher.Stop() here from inside the callback.
                    // On WinRT, Stop() is synchronous and waits for all in-flight event
                    // handlers — calling it from within the handler itself deadlocks.
                    cv.notify_one();
                    return;
                }
            }
        });

    watcher.ScanningMode(BluetoothLEScanningMode::Active);
    watcher.Start();

    std::wcout << L"Scanning for Joy-Con... (Waiting up to 30 seconds)\n";

    {
        std::unique_lock<std::mutex> lock(mtx);
        // wait_for checks the predicate before sleeping, so a notification that
        // arrived before we started waiting is not lost.
        if (!cv.wait_for(lock, std::chrono::seconds(30), [&]() { return connected.load(std::memory_order_acquire); }))
        {
            std::wcerr << L"Timeout: Joy-Con not found.\n";
            // Stop the watcher from the main thread (safe — not inside callback)
            watcher.Stop();
            exit(1);
        }
    }
    // Stop the watcher from the main thread after wait_for returns (safe path)
    watcher.Stop();

    cj.device = device;

    auto servicesResult = device.GetGattServicesAsync().get();
    if (servicesResult.Status() != GattCommunicationStatus::Success)
    {
        std::wcerr << L"Failed to get GATT services.\n";
        exit(1);
    }

    for (auto service : servicesResult.Services())
    {
        auto charsResult = service.GetCharacteristicsAsync().get();
        if (charsResult.Status() != GattCommunicationStatus::Success) continue;
        for (auto characteristic : charsResult.Characteristics())
        {
            if (characteristic.Uuid() == guid(INPUT_REPORT_UUID))
                cj.inputChar = characteristic;
            else if (characteristic.Uuid() == guid(WRITE_COMMAND_UUID))
                cj.writeChar = characteristic;
        }
    }

    return cj;
}

enum ControllerType {
    SingleJoyCon = 1,
    DualJoyCon = 2,
    ProController = 3,
    NSOGCController = 4
};

struct PlayerConfig {
    ControllerType controllerType;
    JoyConSide joyconSide;
    JoyConOrientation joyconOrientation;
    GyroSource gyroSource;
};

// For single Joy-Con players, store controller + JoyCon info to keep alive
struct SingleJoyConPlayer {
    ConnectedJoyCon joycon;
    PVIGEM_TARGET ds4Controller;
    JoyConSide side;
    JoyConOrientation orientation;
    
    // Mouse State
    int mouseMode = 0; // 0=Off, 1=Fast, 2=Normal, 3=Slow
    bool wasChatPressed = false;
    int16_t lastOpticalX = 0;
    int16_t lastOpticalY = 0;
    bool firstOpticalRead = true;
    
    // Scroll Accumulator
    float scrollAccumulator = 0.0f;
    
    // Button States for Edge Detection (to avoid rapid fire)
    bool mb4Pressed = false;
    bool mb5Pressed = false;
    
    // Previous button states for click emulation
    bool leftBtnPressed = false;
    bool rightBtnPressed = false;
    bool middleBtnPressed = false;
};

// For dual Joy-Con players, store both JoyCons, controller, thread, and running flag
struct DualJoyConPlayer {
    ConnectedJoyCon leftJoyCon;
    ConnectedJoyCon rightJoyCon;
    GyroSource gyroSource;
    PVIGEM_TARGET ds4Controller;
    std::atomic<bool> running;
    std::thread updateThread;
    // Buffers stored here so their lifetime matches the player (not the loop block)
    std::atomic<std::shared_ptr<std::vector<uint8_t>>> leftBufferAtomic{ std::make_shared<std::vector<uint8_t>>() };
    std::atomic<std::shared_ptr<std::vector<uint8_t>>> rightBufferAtomic{ std::make_shared<std::vector<uint8_t>>() };
};

// For Pro Controller players
struct ProControllerPlayer {
    ConnectedJoyCon controller;
    PVIGEM_TARGET ds4Controller;
};

// Declare the Pro Controller report generator (implement in JoyConDecoder.cpp)
DS4_REPORT_EX GenerateProControllerReport(const std::vector<uint8_t>& buffer);

int main()
{
    init_apartment();

    int numPlayers;
    std::wcout << L"How many players? ";
    std::wcin >> numPlayers;
    std::wcin.ignore();

    std::vector<PlayerConfig> playerConfigs;

    for (int i = 0; i < numPlayers; ++i) {
        PlayerConfig config{};
        std::wstring line;

        while (true) {
            std::wcout << L"Player " << (i + 1) << L":\n";
            std::wcout << L"  What controller type? (1=Single JoyCon, 2=Dual JoyCon, 3=Pro Controller, 4=NSO GC Controller): ";
            std::getline(std::wcin, line);
            if (line == L"1" || line == L"2" || line == L"3" || line == L"4") {
                config.controllerType = static_cast<ControllerType>(std::stoi(std::string(line.begin(), line.end())));
                break;
            }
            std::wcout << L"Invalid input. Please enter 1, 2, or 3.\n";
        }

        if (config.controllerType == SingleJoyCon) {
            while (true) {
                std::wcout << L"  Which side? (L=Left, R=Right): ";
                std::getline(std::wcin, line);
                if (line == L"L" || line == L"R" || line == L"l" || line == L"r") {
                    config.joyconSide = (line == L"L" || line == L"l") ? JoyConSide::Left : JoyConSide::Right;
                    break;
                }
                std::wcout << L"Invalid input. Please enter L or R.\n";
            }
            while (true) {
                std::wcout << L"  What orientation? (U=Upright, S=Sideways): ";
                std::getline(std::wcin, line);
                if (line == L"U" || line == L"S" || line == L"u" || line == L"s") {
                    config.joyconOrientation = (line == L"S" || line == L"s") ? JoyConOrientation::Sideways : JoyConOrientation::Upright;
                    break;
                }
                std::wcout << L"Invalid input. Please enter U or S.\n";
            }
        }
        else if (config.controllerType == DualJoyCon) {
            config.joyconSide = JoyConSide::Left;
            config.joyconOrientation = JoyConOrientation::Upright;

            while (true) {
                std::wcout << L"Player " << (i + 1) << L":\n";
                std::wcout << L"  What should be used as Gyro Source? (B=Both JoyCons, L=Left JoyCon, R=Right JoyCon): ";
                std::getline(std::wcin, line);

                if (line == L"B") {
                    config.gyroSource = GyroSource::Both;
                    break;
                }
                else if (line == L"L") {
                    config.gyroSource = GyroSource::Left;
                    break;
                }
                else if (line == L"R") {
                    config.gyroSource = GyroSource::Right;
                    break;
                }
                std::wcout << L"Invalid input. Please enter B, L, or R.\n";
            }

        }

        playerConfigs.push_back(config);
    }

    InitializeViGEm();

    // Store all players to keep them alive
    std::vector<std::unique_ptr<SingleJoyConPlayer>> singlePlayers;
    std::vector<std::unique_ptr<DualJoyConPlayer>> dualPlayers;
    std::vector<ProControllerPlayer> proPlayers;

    for (int i = 0; i < numPlayers; ++i) {
        auto& config = playerConfigs[i];
        std::wcout << L"Player " << (i + 1) << L" setup...\n";

        if (config.controllerType == SingleJoyCon) {
            std::wstring sideStr = (config.joyconSide == JoyConSide::Left) ? L"Left" : L"Right";
            std::wcout << L"Please sync your single Joy-Con (" << sideStr << L") now.\n";

            ConnectedJoyCon cj = WaitForJoyCon(L"Waiting for single Joy-Con...");

            // Request minimum BLE connection interval for lowest latency
            try {
                auto connectionParams = BluetoothLEPreferredConnectionParameters::ThroughputOptimized();
                cj.device.RequestPreferredConnectionParameters(connectionParams);
                std::wcout << L"Requested ThroughputOptimized connection parameters for lower latency.\n";
            }
            catch (...) {
                std::wcout << L"Warning: Could not request preferred connection parameters.\n";
            }

            PVIGEM_TARGET ds4_controller = vigem_target_ds4_alloc();
            auto ret = vigem_target_add(vigem_client, ds4_controller);
            if (!VIGEM_SUCCESS(ret))
            {
                std::wcerr << L"Failed to add DS4 controller target: 0x" << std::hex << ret << L"\n";
                exit(1);
            }

            singlePlayers.push_back(std::make_unique<SingleJoyConPlayer>(SingleJoyConPlayer{ cj, ds4_controller, config.joyconSide, config.joyconOrientation }));
            auto& player = *singlePlayers.back();

            player.joycon.inputChar.ValueChanged([joyconSide = player.side, joyconOrientation = player.orientation, &player](GattCharacteristic const&, GattValueChangedEventArgs const& args)
                {
                    auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                    std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
                    reader.ReadBytes(buffer);

                    // Optical Mouse Toggle Logic (Only for Right Joy-Con/Joy-Con 2)
                    if (joyconSide == JoyConSide::Right) {
                        uint32_t btnState = ExtractButtonState(buffer);
                        // CHAT button mask 0x000040 (Right JoyCon)
                        bool chatPressed = (btnState & 0x000040) != 0;

                        if (chatPressed && !player.wasChatPressed) {
                            player.mouseMode = (player.mouseMode + 1) % 4;
                            const char* modeName = "OFF";
                            uint8_t ledPattern = 0x01; // Default OFF = LED 1
                            if (player.mouseMode == 1) {
                                modeName = "FAST";
                                ledPattern = 0x02; // LED 2
                            }
                            else if (player.mouseMode == 2) {
                                modeName = "NORMAL";
                                ledPattern = 0x04; // LED 3
                            }
                            else if (player.mouseMode == 3) {
                                modeName = "SLOW";
                                ledPattern = 0x08; // LED 4
                            }
                            
                            std::cout << "Optical Mouse Mode: " << modeName << std::endl;
                            SetPlayerLEDs(player.joycon.writeChar, ledPattern);
                            EmitSound(player.joycon.writeChar);
                        }
                        player.wasChatPressed = chatPressed;
                        
                        // If Mouse Mode is ON (1, 2, or 3)
                        if (player.mouseMode > 0) {
                            // --- 1. Optical Mouse Movement ---
                            auto [rawX, rawY] = GetRawOpticalMouse(buffer);
                            if (player.firstOpticalRead) {
                                player.lastOpticalX = rawX;
                                player.lastOpticalY = rawY;
                                player.firstOpticalRead = false;
                            } else {
                                int16_t dx = rawX - player.lastOpticalX;
                                int16_t dy = rawY - player.lastOpticalY;
                                player.lastOpticalX = rawX;
                                player.lastOpticalY = rawY;

                                if (dx != 0 || dy != 0) {
                                    float sensitivity = 1.0f;
                                    if (player.mouseMode == 1) sensitivity = 1.0f;      // TODO: Fast 可调
                                    else if (player.mouseMode == 2) sensitivity = 0.6f; // TODO: Normal 可调
                                    else if (player.mouseMode == 3) sensitivity = 0.3f; // TODO: Slow 可调
                                    
                                    int moveX = static_cast<int>(dx * sensitivity);
                                    int moveY = static_cast<int>(dy * sensitivity);
                                    
                                    INPUT input = {};
                                    input.type = INPUT_MOUSE;
                                    input.mi.dx = moveX;
                                    input.mi.dy = moveY;
                                    input.mi.dwFlags = MOUSEEVENTF_MOVE;
                                    SendInput(1, &input, sizeof(INPUT));
                                }
                            }
                            
                            // --- 2. Mouse Buttons (R=Left, ZR=Right, Stick=Middle) ---
                            // R = 0x004000, ZR = 0x008000, Stick = 0x000004
                            bool rPressed = (btnState & 0x004000) != 0;
                            bool zrPressed = (btnState & 0x008000) != 0;
                            bool stickPressed = (btnState & 0x000004) != 0;
                            
                            // Left Click (R)
                            if (rPressed && !player.leftBtnPressed) {
                                INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; SendInput(1, &input, sizeof(INPUT));
                            } else if (!rPressed && player.leftBtnPressed) {
                                INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_LEFTUP; SendInput(1, &input, sizeof(INPUT));
                            }
                            player.leftBtnPressed = rPressed;
                            
                            // Right Click (ZR)
                            if (zrPressed && !player.rightBtnPressed) {
                                INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; SendInput(1, &input, sizeof(INPUT));
                            } else if (!zrPressed && player.rightBtnPressed) {
                                INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_RIGHTUP; SendInput(1, &input, sizeof(INPUT));
                            }
                            player.rightBtnPressed = zrPressed;
                            
                            // Middle Click (Stick)
                            if (stickPressed && !player.middleBtnPressed) {
                                INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN; SendInput(1, &input, sizeof(INPUT));
                            } else if (!stickPressed && player.middleBtnPressed) {
                                INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP; SendInput(1, &input, sizeof(INPUT));
                            }
                            player.middleBtnPressed = stickPressed;
                            
                            // --- 3. Stick Scrolling & Side Buttons ---
                            auto stickData = DecodeJoystick(buffer, joyconSide, joyconOrientation);
                            
                            const int SCROLL_DEADZONE = 4000;
                            if (abs(stickData.y) > SCROLL_DEADZONE) {
                                // Accumulate scroll
                                float intensity = (abs(stickData.y) - SCROLL_DEADZONE) / (32767.0f - SCROLL_DEADZONE);
                                float speed = intensity * 40.0f; // Max scroll per frame // TODO: 滚轮速度可调
                                if (stickData.y > 0) player.scrollAccumulator -= speed; // Up
                                else player.scrollAccumulator += speed; // Down
                                
                                if (abs(player.scrollAccumulator) >= 120.0f) {
                                    int clicks = static_cast<int>(player.scrollAccumulator / 120.0f);
                                    player.scrollAccumulator -= (clicks * 120.0f);
                                    
                                    INPUT input = {};
                                    input.type = INPUT_MOUSE;
                                    input.mi.mouseData = clicks * 120;
                                    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
                                    SendInput(1, &input, sizeof(INPUT));
                                }
                            } else {
                                player.scrollAccumulator = 0.0f;
                            }
                            
                            // Side Buttons (Horizontal Stick)
                            // Left Peak -> Back (MB4)
                            // Right Peak -> Forward (MB5)
                            const int BUTTON_THRESHOLD = 28000; // Near edge
                            
                            // MB4 (Back) - Left
                            if (stickData.x < -BUTTON_THRESHOLD) {
                                if (!player.mb4Pressed) {
                                    INPUT input = {}; input.type = INPUT_MOUSE; input.mi.mouseData = XBUTTON1; input.mi.dwFlags = MOUSEEVENTF_XDOWN; SendInput(1, &input, sizeof(INPUT));
                                    INPUT input2 = {}; input2.type = INPUT_MOUSE; input2.mi.mouseData = XBUTTON1; input2.mi.dwFlags = MOUSEEVENTF_XUP; SendInput(1, &input2, sizeof(INPUT));
                                    player.mb4Pressed = true;
                                }
                            } else {
                                player.mb4Pressed = false;
                            }
                            
                            // MB5 (Forward) - Right
                            if (stickData.x > BUTTON_THRESHOLD) {
                                if (!player.mb5Pressed) {
                                    INPUT input = {}; input.type = INPUT_MOUSE; input.mi.mouseData = XBUTTON2; input.mi.dwFlags = MOUSEEVENTF_XDOWN; SendInput(1, &input, sizeof(INPUT));
                                    INPUT input2 = {}; input2.type = INPUT_MOUSE; input2.mi.mouseData = XBUTTON2; input2.mi.dwFlags = MOUSEEVENTF_XUP; SendInput(1, &input2, sizeof(INPUT));
                                    player.mb5Pressed = true;
                                }
                            } else {
                                player.mb5Pressed = false;
                            }
                            
                            // --- 4. Suppress Inputs in DS4 Report ---
                            // Modify buffer to clear mapped buttons so they don't trigger game actions
                            // R (Byte 3, bit 6: 0x40), ZR (Byte 4, bit 7: 0x80 ... wait, let's check masks)
                            
                            // Button masks again:
                            // R: 0x004000 -> Byte 3 & 0x40.
                            // ZR: 0x008000 -> Byte 4 & 0x80 (Wait, 0x008000 is 3rd byte of 24-bit? 3,4,5. 0x004000 is 0x40 << 8? No)
                            // ExtractButtonState: (buffer[3] << 16) | (buffer[4] << 8) | buffer[5]
                            // 0x004000 = Bit 14 set.
                            // buffer[3] is bits 23-16. buffer[4] is 15-8. buffer[5] is 7-0.
                            // 0x4000 is in buffer[4] (0x40 << 8).
                            // 0x8000 is in buffer[4] (0x80 << 8).
                            // Stick Click 0x04 is in buffer[5].
                            
                            // So:
                            // Clear R bit (0x40 in buffer[4])
                            buffer[4] &= ~0x40;
                            // Clear ZR bit (0x80 in buffer[4])
                            buffer[4] &= ~0x80;
                            // Clear Stick Click (0x04 in buffer[5])
                            buffer[5] &= ~0x04;
                            
                            // Clear Stick Data (Set to center)
                            // Right stick bytes: 13, 14, 15
                            if (buffer.size() >= 16) {
                                buffer[13] = 0x00;
                                buffer[14] = 0x08;
                                buffer[15] = 0x80;
                            }
                        } else {
                            // Mode Off: Reset first reads
                            player.firstOpticalRead = true;
                        }
                    }

                    DS4_REPORT_EX report = GenerateDS4Report(buffer, joyconSide, joyconOrientation);

                    auto ret = vigem_target_ds4_update_ex(vigem_client, player.ds4Controller, report);
                    if (!VIGEM_SUCCESS(ret)) {
                         // std::wcerr << L"Failed to update DS4 EX report: 0x" << std::hex << ret << L"\n";
                    }
                });

            auto status = player.joycon.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

            if (player.joycon.writeChar) {
                SendCustomCommands(player.joycon.writeChar);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                SetPlayerLEDs(player.joycon.writeChar, 0x01); // Player 1 (Solid LED 1)
                EmitSound(player.joycon.writeChar);
            }

            if (status == GattCommunicationStatus::Success)
                std::wcout << L"Notifications enabled.\n";
            else
                std::wcout << L"Failed to enable notifications.\n";

            std::wcout << L"Press Enter to continue...\n";
            std::wstring dummy;
            std::getline(std::wcin, dummy);
        }
        else if (config.controllerType == DualJoyCon) {
            std::wcout << L"Please sync your RIGHT Joy-Con now.\n";
            ConnectedJoyCon rightJoyCon = WaitForJoyCon(L"Waiting for RIGHT Joy-Con...");
            if (rightJoyCon.writeChar) {
                SendCustomCommands(rightJoyCon.writeChar);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                SetPlayerLEDs(rightJoyCon.writeChar, 0x01);
                EmitSound(rightJoyCon.writeChar);
            }

            // Request minimum BLE connection interval for right Joy-Con
            try {
                auto connectionParams = BluetoothLEPreferredConnectionParameters::ThroughputOptimized();
                rightJoyCon.device.RequestPreferredConnectionParameters(connectionParams);
                std::wcout << L"Requested ThroughputOptimized connection parameters for RIGHT Joy-Con.\n";
            }
            catch (...) {
                std::wcout << L"Warning: Could not request preferred connection parameters for RIGHT Joy-Con.\n";
            }

            std::wcout << L"Please sync your LEFT Joy-Con now.\n";
            ConnectedJoyCon leftJoyCon = WaitForJoyCon(L"Waiting for LEFT Joy-Con...");
            if (leftJoyCon.writeChar) {
                SendCustomCommands(leftJoyCon.writeChar);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                SetPlayerLEDs(leftJoyCon.writeChar, 0x08); // Player 2 (Solid LED 4? or specific pattern for left?)
                EmitSound(leftJoyCon.writeChar);
            }

            // Request minimum BLE connection interval for left Joy-Con
            try {
                auto connectionParams = BluetoothLEPreferredConnectionParameters::ThroughputOptimized();
                leftJoyCon.device.RequestPreferredConnectionParameters(connectionParams);
                std::wcout << L"Requested ThroughputOptimized connection parameters for LEFT Joy-Con.\n";
            }
            catch (...) {
                std::wcout << L"Warning: Could not request preferred connection parameters for LEFT Joy-Con.\n";
            }

            PVIGEM_TARGET ds4Controller = vigem_target_ds4_alloc();
            auto ret = vigem_target_add(vigem_client, ds4Controller);
            if (!VIGEM_SUCCESS(ret))
            {
                std::wcerr << L"Failed to add DS4 controller target: 0x" << std::hex << ret << L"\n";
                exit(1);
            }

            auto dualPlayer = std::make_unique<DualJoyConPlayer>();
            dualPlayer->leftJoyCon = leftJoyCon;
            dualPlayer->rightJoyCon = rightJoyCon;
            dualPlayer->gyroSource = config.gyroSource;
            dualPlayer->ds4Controller = ds4Controller;
            dualPlayer->running.store(true);

            // NOTE: leftBufferAtomic and rightBufferAtomic now live inside dualPlayer
            // (not as local stack variables), so the update thread cannot outlive them.
            dualPlayer->leftJoyCon.inputChar.ValueChanged([dualPlayerPtr = dualPlayer.get()](GattCharacteristic const&, GattValueChangedEventArgs const& args)
                {
                    auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                    auto buf = std::make_shared<std::vector<uint8_t>>(reader.UnconsumedBufferLength());
                    reader.ReadBytes(*buf);
                    dualPlayerPtr->leftBufferAtomic.store(buf, std::memory_order_release);
                });

            auto statusLeft = dualPlayer->leftJoyCon.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

            if (statusLeft == GattCommunicationStatus::Success)
                std::wcout << L"LEFT Joy-Con notifications enabled.\n";
            else
                std::wcout << L"Failed to enable LEFT Joy-Con notifications.\n";

            dualPlayer->rightJoyCon.inputChar.ValueChanged([dualPlayerPtr = dualPlayer.get()](GattCharacteristic const&, GattValueChangedEventArgs const& args)
                {
                    auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                    auto buf = std::make_shared<std::vector<uint8_t>>(reader.UnconsumedBufferLength());
                    reader.ReadBytes(*buf);
                    dualPlayerPtr->rightBufferAtomic.store(buf, std::memory_order_release);
                });

            auto statusRight = dualPlayer->rightJoyCon.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

            if (statusRight == GattCommunicationStatus::Success)
                std::wcout << L"RIGHT Joy-Con notifications enabled.\n";
            else
                std::wcout << L"Failed to enable RIGHT Joy-Con notifications.\n";

            dualPlayer->updateThread = std::thread([dualPlayerPtr = dualPlayer.get()]()
                {
                    while (dualPlayerPtr->running.load(std::memory_order_acquire))
                    {
                        auto leftBuf = dualPlayerPtr->leftBufferAtomic.load(std::memory_order_acquire);
                        auto rightBuf = dualPlayerPtr->rightBufferAtomic.load(std::memory_order_acquire);

                        if (leftBuf->empty() || rightBuf->empty())
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                            continue;
                        }

                        DS4_REPORT_EX report = GenerateDualJoyConDS4Report(*leftBuf, *rightBuf, dualPlayerPtr->gyroSource);

                        auto ret = vigem_target_ds4_update_ex(vigem_client, dualPlayerPtr->ds4Controller, report);
                        if (!VIGEM_SUCCESS(ret))
                        {
                            std::wcerr << L"Failed to update DS4 report: 0x" << std::hex << ret << L"\n";
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60Hz
                    }
                });

            dualPlayers.push_back(std::move(dualPlayer));

            std::wcout << L"Dual Joy-Cons connected and configured. Press Enter to continue...\n";
            std::wstring dummy;
            std::getline(std::wcin, dummy);
        }
        else if (config.controllerType == ProController) {
            // Load or create GL/GR layouts configuration
            if (!LoadProControllerConfig(g_proControllerConfig)) {
                std::cout << "\nNo existing configuration found. Creating default layout...\n";
                CreateDefaultConfig();
                std::cout << "Default layout created: Layout 1 (GL: NONE, GR: NONE)\n";
                std::cout << "Press ZL+ZR+GL+GR during gameplay to open layout management.\n\n";
            } else {
                std::cout << "\nLoaded GL/GR layout configuration:\n";
                std::cout << "Active Layout: " << g_proControllerConfig.layouts[g_proControllerConfig.activeLayoutIndex].name << "\n";
                std::cout << "  GL -> " << ButtonMappingToString(g_proControllerConfig.layouts[g_proControllerConfig.activeLayoutIndex].glMapping) << "\n";
                std::cout << "  GR -> " << ButtonMappingToString(g_proControllerConfig.layouts[g_proControllerConfig.activeLayoutIndex].grMapping) << "\n";
                std::cout << "Total layouts: " << g_proControllerConfig.layouts.size() << "\n";
                std::cout << "Press ZL+ZR+GL+GR during gameplay to open layout management.\n";
                std::cout << "Press C button to cycle through layouts.\n\n";
            }

            std::wcout << L"Please sync your Pro Controller now.\n";

            ConnectedJoyCon proController = WaitForJoyCon(L"Waiting for Pro Controller...");

            // Request minimum BLE connection interval for lowest latency
            try {
                auto connectionParams = BluetoothLEPreferredConnectionParameters::ThroughputOptimized();
                auto paramResult = proController.device.RequestPreferredConnectionParameters(connectionParams);

                std::wcout << L"Requested ThroughputOptimized connection parameters for lower latency.\n";
            }
            catch (...) {
                std::wcout << L"Warning: Could not request preferred connection parameters. Continuing with default settings.\n";
            }

            PVIGEM_TARGET ds4_controller = vigem_target_ds4_alloc();
            auto ret = vigem_target_add(vigem_client, ds4_controller);
            if (!VIGEM_SUCCESS(ret))
            {
                std::wcerr << L"Failed to add DS4 controller target: 0x" << std::hex << ret << L"\n";
                exit(1);
            }

            proController.inputChar.ValueChanged([ds4_controller](GattCharacteristic const&, GattValueChangedEventArgs const& args) mutable
                {
                    auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                    std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
                    reader.ReadBytes(buffer);


                    DS4_REPORT_EX report = GenerateProControllerReport(buffer);

                    // Apply GL/GR button mappings
                    ApplyGLGRMappings(report, buffer);

                    // Handle special buttons (Screenshot -> F12, C button)
                    HandleSpecialProButtons(buffer);

                    auto ret = vigem_target_ds4_update_ex(vigem_client, ds4_controller, report);
                    if (!VIGEM_SUCCESS(ret)) {
                        std::wcerr << L"Failed to update DS4 EX report: 0x" << std::hex << ret << L"\n";
                    }
                });

            auto status = proController.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

            if (proController.writeChar) {
                SendCustomCommands(proController.writeChar);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                SetPlayerLEDs(proController.writeChar, 0x01);
                EmitSound(proController.writeChar);
            }

            if (status == GattCommunicationStatus::Success)
                std::wcout << L"Pro Controller notifications enabled.\n";
            else
                std::wcout << L"Failed to enable Pro Controller notifications.\n";

            std::wcout << L"Press Enter to continue...\n";
            std::wstring dummy;
            std::getline(std::wcin, dummy);

            proPlayers.push_back({ proController, ds4_controller });
        }
        else if (config.controllerType == NSOGCController) {
            std::wcout << L"Please sync your NSO GameCube Controller now.\n";

            ConnectedJoyCon gcController = WaitForJoyCon(L"Waiting for NSO GC Controller...");

            // Request minimum BLE connection interval for lowest latency
            try {
                auto connectionParams = BluetoothLEPreferredConnectionParameters::ThroughputOptimized();
                gcController.device.RequestPreferredConnectionParameters(connectionParams);
                std::wcout << L"Requested ThroughputOptimized connection parameters for lower latency.\n";
            }
            catch (...) {
                std::wcout << L"Warning: Could not request preferred connection parameters.\n";
            }

            PVIGEM_TARGET ds4_controller = vigem_target_ds4_alloc();
            auto ret = vigem_target_add(vigem_client, ds4_controller);
            if (!VIGEM_SUCCESS(ret)) {
                std::wcerr << L"Failed to add DS4 controller target: 0x" << std::hex << ret << L"\n";
                exit(1);
            }

            gcController.inputChar.ValueChanged([ds4_controller](GattCharacteristic const&, GattValueChangedEventArgs const& args) mutable {
                auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
                reader.ReadBytes(buffer);

                DS4_REPORT_EX report = GenerateNSOGCReport(buffer);

                auto ret = vigem_target_ds4_update_ex(vigem_client, ds4_controller, report);
                if (!VIGEM_SUCCESS(ret)) {
                    std::wcerr << L"Failed to update DS4 EX report: 0x" << std::hex << ret << L"\n";
                }
                });

            auto status = gcController.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

            if (gcController.writeChar) {
                SendCustomCommands(gcController.writeChar); // Optional, only if NSO GC expects init commands
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                SetPlayerLEDs(gcController.writeChar, 0x01);
                EmitSound(gcController.writeChar);
            }

            if (status == GattCommunicationStatus::Success)
                std::wcout << L"NSO GC Controller notifications enabled.\n";
            else
                std::wcout << L"Failed to enable NSO GC Controller notifications.\n";

            std::wcout << L"Press Enter to continue...\n";
            std::wstring dummy;
            std::getline(std::wcin, dummy);

            proPlayers.push_back({ gcController, ds4_controller }); // reuse ProControllerPlayer struct
}
    }

    std::wcout << L"All players connected.\n";
    std::wcout << L"- Press Enter to exit\n";
    std::wcout << L"- Press ZL+ZR+GL+GR on Pro Controller to open layout management\n";
    std::wcout << L"- Press C button on Pro Controller to cycle layouts\n\n";

    // Main loop: monitor for management window trigger and exit command
    while (true) {
        // Check if management window should be opened
        if (g_openManagementWindow.load()) {
            g_openManagementWindow.store(false);
            std::cout << "\n[ZL+ZR+GL+GR combo detected! Opening layout management...]\n";
            ShowLayoutManagementWindow();
            std::cout << "\nResuming gameplay...\n";
            std::cout << "Active layout: " << g_proControllerConfig.layouts[g_proControllerConfig.activeLayoutIndex].name << "\n";
            std::cout << "Press Enter to exit, or ZL+ZR+GL+GR to manage layouts again.\n\n";
        }

        // Check for user input (non-blocking check)
        // Use a short sleep and check if Enter was pressed
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check if input is available (simple approach for Windows)
        if (_kbhit()) {
            std::wstring dummy;
            std::getline(std::wcin, dummy);
            break;
        }
    }

    // Clean up dual player threads & free controllers
    for (auto& dp : dualPlayers)
    {
        dp->running.store(false);
        if (dp->updateThread.joinable())
            dp->updateThread.join();

        vigem_target_remove(vigem_client, dp->ds4Controller);
        vigem_target_free(dp->ds4Controller);
    }

    // Free single players controllers
    for (auto& sp : singlePlayers)
    {
        vigem_target_remove(vigem_client, sp->ds4Controller);
        vigem_target_free(sp->ds4Controller);
    }

    // Free Pro Controllers
    for (auto& pp : proPlayers)
    {
        vigem_target_remove(vigem_client, pp.ds4Controller);
        vigem_target_free(pp.ds4Controller);
    }

    if (vigem_client)
    {
        vigem_disconnect(vigem_client);
        vigem_free(vigem_client);
        vigem_client = nullptr;
    }

    return 0;
}

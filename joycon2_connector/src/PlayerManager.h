#pragma once
// PlayerManager - Central management of all connected controllers
#include "DeviceManager.h"
#include "ViGEmManager.h"
#include "BLECommands.h"
#include "ConfigManager.h"
#include "JoyConDecoder.h"
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <Windows.h>

// Vibration callback context passed to ViGEm as UserData
struct VibrationContext {
    GattCharacteristic writeChar{ nullptr };
    GattCharacteristic writeCharLeft{ nullptr };   // for dual joycon
    GattCharacteristic writeCharRight{ nullptr };  // for dual joycon
    bool isDual = false;
    std::chrono::steady_clock::time_point lastSendTime{};
    uint8_t lastSample = 0xFF;  // track to avoid redundant sends
    static constexpr int MIN_INTERVAL_MS = 50;     // throttle BLE writes
};

// ViGEm DS4 vibration notification callback (runs on ViGEm worker thread)
inline VOID CALLBACK DS4VibrationCallback(
    PVIGEM_CLIENT /*Client*/,
    PVIGEM_TARGET /*Target*/,
    UCHAR LargeMotor,
    UCHAR SmallMotor,
    DS4_LIGHTBAR_COLOR /*LightbarColor*/,
    LPVOID UserData)
{
    auto* ctx = static_cast<VibrationContext*>(UserData);
    if (!ctx) return;

    auto& vibConfig = ConfigManager::Instance().config.vibrationConfig;
    if (!vibConfig.enabled) return;

    // Throttle: skip if too soon since last send
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx->lastSendTime).count();
    if (elapsed < VibrationContext::MIN_INTERVAL_MS) return;

    // Apply intensity scaling
    float scaledLarge = LargeMotor * vibConfig.intensity;
    float scaledSmall = SmallMotor * vibConfig.intensity;
    uint8_t motorL = static_cast<uint8_t>((std::min)(scaledLarge, 255.0f));
    uint8_t motorR = static_cast<uint8_t>((std::min)(scaledSmall, 255.0f));

    // Map motor values to predefined vibration samples
    uint8_t sample;
    if (motorL == 0 && motorR == 0) {
        sample = VIB_NONE;
    } else if (motorL > 180 || motorR > 180) {
        sample = VIB_BUZZ;           // Strong sustained vibration
    } else if (motorL > 80 || motorR > 80) {
        sample = VIB_STRONG_THUNK;   // Medium impact
    } else {
        sample = VIB_DUN;            // Light feedback
    }

    // Skip if same sample as last sent
    if (sample == ctx->lastSample && sample != VIB_NONE) return;
    ctx->lastSample = sample;
    ctx->lastSendTime = now;

    if (ctx->isDual) {
        // Dual JoyCon: large motor -> left, small motor -> right
        if (ctx->writeCharLeft && motorL > 0) {
            SendVibrationSampleAsync(ctx->writeCharLeft, sample);
        }
        if (ctx->writeCharRight && motorR > 0) {
            SendVibrationSampleAsync(ctx->writeCharRight, sample);
        }
        if (motorL == 0 && motorR == 0) {
            if (ctx->writeCharLeft)  SendVibrationSampleAsync(ctx->writeCharLeft, VIB_NONE);
            if (ctx->writeCharRight) SendVibrationSampleAsync(ctx->writeCharRight, VIB_NONE);
        }
    } else {
        if (ctx->writeChar) {
            SendVibrationSampleAsync(ctx->writeChar, sample);
        }
    }
}

enum class ControllerType {
    SingleJoyCon = 1,
    DualJoyCon = 2,
    ProController = 3,
    NSOGCController = 4
};

struct PlayerConfig {
    ControllerType controllerType;
    JoyConSide joyconSide = JoyConSide::Left;
    JoyConOrientation joyconOrientation = JoyConOrientation::Upright;
    GyroSource gyroSource = GyroSource::Both;
};

struct SingleJoyConPlayer {
    ConnectedJoyCon joycon;
    PVIGEM_TARGET ds4Controller = nullptr;
    JoyConSide side;
    JoyConOrientation orientation;
    // Per-device settings
    bool swapABXY = false;
    uint64_t bleAddress = 0;
    // Mouse State
    int mouseMode = 0;
    bool wasChatPressed = false;
    int16_t lastOpticalX = 0;
    int16_t lastOpticalY = 0;
    bool firstOpticalRead = true;
    float scrollAccumulator = 0.0f;
    bool mb4Pressed = false;
    bool mb5Pressed = false;
    bool leftBtnPressed = false;
    bool rightBtnPressed = false;
    bool middleBtnPressed = false;
    // Sub-pixel accumulation for smooth mouse movement (direct mode fallback)
    float accumX = 0.0f;
    float accumY = 0.0f;
    // Vibration context for ViGEm callback
    std::unique_ptr<VibrationContext> vibCtx;
    // Interpolation state for high-frequency mouse output
    std::atomic<float> pendingDX{ 0.0f };
    std::atomic<float> pendingDY{ 0.0f };
    std::atomic<bool> newReportReady{ false };
    std::atomic<bool> mouseInterpolActive{ false };
    std::chrono::steady_clock::time_point lastBLETimestamp{};
    std::atomic<float> reportIntervalMs{ 15.0f };
    bool bleTimestampInitialized = false;

    // Move constructor & assignment (std::atomic is non-copyable)
    SingleJoyConPlayer() = default;
    SingleJoyConPlayer(ConnectedJoyCon cj_, PVIGEM_TARGET ds4_, JoyConSide side_, JoyConOrientation orient_)
        : joycon(std::move(cj_)), ds4Controller(ds4_), side(side_), orientation(orient_) {}
    SingleJoyConPlayer(SingleJoyConPlayer&& o) noexcept
        : joycon(std::move(o.joycon)), ds4Controller(o.ds4Controller),
          side(o.side), orientation(o.orientation),
          mouseMode(o.mouseMode), wasChatPressed(o.wasChatPressed),
          lastOpticalX(o.lastOpticalX), lastOpticalY(o.lastOpticalY),
          firstOpticalRead(o.firstOpticalRead), scrollAccumulator(o.scrollAccumulator),
          mb4Pressed(o.mb4Pressed), mb5Pressed(o.mb5Pressed),
          leftBtnPressed(o.leftBtnPressed), rightBtnPressed(o.rightBtnPressed),
          middleBtnPressed(o.middleBtnPressed), accumX(o.accumX), accumY(o.accumY),
          vibCtx(std::move(o.vibCtx)),
          pendingDX(o.pendingDX.load()), pendingDY(o.pendingDY.load()),
          newReportReady(o.newReportReady.load()), mouseInterpolActive(o.mouseInterpolActive.load()),
          lastBLETimestamp(o.lastBLETimestamp),
          reportIntervalMs(o.reportIntervalMs.load()),
          bleTimestampInitialized(o.bleTimestampInitialized) {}
    SingleJoyConPlayer& operator=(SingleJoyConPlayer&& o) noexcept {
        if (this != &o) {
            joycon = std::move(o.joycon); ds4Controller = o.ds4Controller;
            side = o.side; orientation = o.orientation;
            mouseMode = o.mouseMode; wasChatPressed = o.wasChatPressed;
            lastOpticalX = o.lastOpticalX; lastOpticalY = o.lastOpticalY;
            firstOpticalRead = o.firstOpticalRead; scrollAccumulator = o.scrollAccumulator;
            mb4Pressed = o.mb4Pressed; mb5Pressed = o.mb5Pressed;
            leftBtnPressed = o.leftBtnPressed; rightBtnPressed = o.rightBtnPressed;
            middleBtnPressed = o.middleBtnPressed; accumX = o.accumX; accumY = o.accumY;
            vibCtx = std::move(o.vibCtx);
            pendingDX.store(o.pendingDX.load()); pendingDY.store(o.pendingDY.load());
            newReportReady.store(o.newReportReady.load()); mouseInterpolActive.store(o.mouseInterpolActive.load());
            lastBLETimestamp = o.lastBLETimestamp;
            reportIntervalMs.store(o.reportIntervalMs.load());
            bleTimestampInitialized = o.bleTimestampInitialized;
        }
        return *this;
    }
    SingleJoyConPlayer(const SingleJoyConPlayer&) = delete;
    SingleJoyConPlayer& operator=(const SingleJoyConPlayer&) = delete;
};

struct DualJoyConPlayer {
    ConnectedJoyCon leftJoyCon;
    ConnectedJoyCon rightJoyCon;
    GyroSource gyroSource;
    PVIGEM_TARGET ds4Controller = nullptr;
    // Per-device settings
    bool swapABXY = false;
    uint64_t bleAddress = 0;  // uses right JoyCon's BLE address
    std::atomic<bool> running{ false };
    std::thread updateThread;
    std::atomic<std::shared_ptr<std::vector<uint8_t>>> leftBufferAtomic{ std::make_shared<std::vector<uint8_t>>() };
    std::atomic<std::shared_ptr<std::vector<uint8_t>>> rightBufferAtomic{ std::make_shared<std::vector<uint8_t>>() };
    std::mutex bufferMutex;
    std::condition_variable bufferCV;
    std::unique_ptr<VibrationContext> vibCtx;
};

struct ProControllerPlayer {
    ConnectedJoyCon controller;
    PVIGEM_TARGET ds4Controller = nullptr;
    ControllerType type = ControllerType::ProController; // can also be NSOGCController
    std::unique_ptr<VibrationContext> vibCtx;
    // Per-device settings
    std::shared_ptr<std::atomic<bool>> swapABXYFlag = std::make_shared<std::atomic<bool>>(false);
    uint64_t bleAddress = 0;
};

// Button mapping application
inline void ApplyButtonMapping(DS4_REPORT_EX& report, ButtonMapping mapping) {
    auto& r = report.Report;
    switch (mapping) {
    case ButtonMapping::L3:     r.wButtons |= DS4_BUTTON_THUMB_LEFT; break;
    case ButtonMapping::R3:     r.wButtons |= DS4_BUTTON_THUMB_RIGHT; break;
    case ButtonMapping::L1:     r.wButtons |= DS4_BUTTON_SHOULDER_LEFT; break;
    case ButtonMapping::R1:     r.wButtons |= DS4_BUTTON_SHOULDER_RIGHT; break;
    case ButtonMapping::L2:     r.wButtons |= DS4_BUTTON_TRIGGER_LEFT; r.bTriggerL = 255; break;
    case ButtonMapping::R2:     r.wButtons |= DS4_BUTTON_TRIGGER_RIGHT; r.bTriggerR = 255; break;
    case ButtonMapping::CROSS:  r.wButtons |= DS4_BUTTON_CROSS; break;
    case ButtonMapping::CIRCLE: r.wButtons |= DS4_BUTTON_CIRCLE; break;
    case ButtonMapping::SQUARE: r.wButtons |= DS4_BUTTON_SQUARE; break;
    case ButtonMapping::TRIANGLE: r.wButtons |= DS4_BUTTON_TRIANGLE; break;
    case ButtonMapping::SHARE:  r.wButtons |= DS4_BUTTON_SHARE; break;
    case ButtonMapping::OPTIONS:r.wButtons |= DS4_BUTTON_OPTIONS; break;
    case ButtonMapping::DPAD_UP:    r.wButtons = (r.wButtons & ~0xF) | DS4_BUTTON_DPAD_NORTH; break;
    case ButtonMapping::DPAD_DOWN:  r.wButtons = (r.wButtons & ~0xF) | DS4_BUTTON_DPAD_SOUTH; break;
    case ButtonMapping::DPAD_LEFT:  r.wButtons = (r.wButtons & ~0xF) | DS4_BUTTON_DPAD_WEST; break;
    case ButtonMapping::DPAD_RIGHT: r.wButtons = (r.wButtons & ~0xF) | DS4_BUTTON_DPAD_EAST; break;
    default: break;
    }
}

// Keyboard input helper
inline void SendKeyboardInput(WORD virtualKey, bool keyDown) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = keyDown ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

// GL/GR application for Pro controllers
inline void ApplyGLGRMappings(DS4_REPORT_EX& report, const std::vector<uint8_t>& buffer) {
    if (buffer.size() < 9) return;
    auto& config = ConfigManager::Instance().config.proConfig;
    if (config.layouts.empty()) return;

    int layoutIndex = config.activeLayoutIndex;
    if (layoutIndex < 0 || layoutIndex >= static_cast<int>(config.layouts.size())) {
        layoutIndex = 0;
        config.activeLayoutIndex = 0;
    }

    const GLGRLayout& activeLayout = config.layouts[layoutIndex];
    uint64_t state = 0;
    for (int i = 3; i <= 8; ++i) state = (state << 8) | buffer[i];

    constexpr uint64_t BUTTON_GL_MASK = 0x000000000200;
    constexpr uint64_t BUTTON_GR_MASK = 0x000000000100;

    if (state & BUTTON_GL_MASK) ApplyButtonMapping(report, activeLayout.glMapping);
    if (state & BUTTON_GR_MASK) ApplyButtonMapping(report, activeLayout.grMapping);
}

// Special button handling statics
static bool g_screenshotButtonPressed = false;
static bool g_cButtonPressed = false;
static bool g_comboPressed = false;
static std::atomic<bool> g_openManagementWindow(false);

inline void HandleSpecialProButtons(const std::vector<uint8_t>& buffer) {
    if (buffer.size() < 9) return;
    uint64_t state = 0;
    for (int i = 3; i <= 8; ++i) state = (state << 8) | buffer[i];

    // Screenshot -> F12
    constexpr uint64_t BUTTON_SCREENSHOT_MASK = 0x000000000400;
    bool screenshotPressed = (state & BUTTON_SCREENSHOT_MASK) != 0;
    if (screenshotPressed && !g_screenshotButtonPressed) SendKeyboardInput(VK_F12, true);
    else if (!screenshotPressed && g_screenshotButtonPressed) SendKeyboardInput(VK_F12, false);
    g_screenshotButtonPressed = screenshotPressed;

    // ZL+ZR+GL+GR combo
    constexpr uint64_t TRIGGER_LT = 0x000000800000;
    constexpr uint64_t TRIGGER_RT = 0x008000000000;
    constexpr uint64_t GL_MASK = 0x000000000200;
    constexpr uint64_t GR_MASK = 0x000000000100;
    bool comboActive = (state & TRIGGER_LT) && (state & TRIGGER_RT) && (state & GL_MASK) && (state & GR_MASK);
    if (comboActive && !g_comboPressed) g_openManagementWindow.store(true);
    g_comboPressed = comboActive;

    // C button -> cycle layout
    constexpr uint64_t BUTTON_C_MASK = 0x000000000800;
    bool cPressed = (state & BUTTON_C_MASK) != 0;
    if (cPressed && !g_cButtonPressed) {
        auto& config = ConfigManager::Instance().config.proConfig;
        if (!config.layouts.empty()) {
            config.activeLayoutIndex = (config.activeLayoutIndex + 1) % config.layouts.size();
            ConfigManager::Instance().Save();
        }
    }
    g_cButtonPressed = cPressed;
}

class PlayerManager {
public:
    static PlayerManager& Instance() {
        static PlayerManager inst;
        return inst;
    }

    int GetPlayerCount() const {
        return (int)(singlePlayers.size() + dualPlayers.size() + proPlayers.size());
    }

    // Player data accessors for UI
    std::vector<SingleJoyConPlayer>& GetSinglePlayers() { return singlePlayers; }
    std::vector<std::unique_ptr<DualJoyConPlayer>>& GetDualPlayers() { return dualPlayers; }
    std::vector<ProControllerPlayer>& GetProPlayers() { return proPlayers; }

    // Add a Single JoyCon player from async scan result
    bool AddSingleJoyCon(ConnectedJoyCon cj, JoyConSide side, JoyConOrientation orientation) {
        auto& vigem = ViGEmManager::Instance();
        PVIGEM_TARGET ds4 = vigem.AllocDS4();
        if (!ds4 || !vigem.AddTarget(ds4)) return false;

        singlePlayers.push_back(SingleJoyConPlayer(cj, ds4, side, orientation));
        auto& player = singlePlayers.back();
        player.bleAddress = cj.bleAddress;
        player.swapABXY = ConfigManager::Instance().GetDeviceSettings(cj.bleAddress).swapABXY;
        auto& mouseConfig = ConfigManager::Instance().config.mouseConfig;

        // Register vibration callback
        player.vibCtx = std::make_unique<VibrationContext>();
        player.vibCtx->writeChar = cj.writeChar;
        vigem_target_ds4_register_notification(
            vigem.GetClient(), ds4, DS4VibrationCallback, player.vibCtx.get());

        player.joycon.inputChar.ValueChanged(
            [joyconSide = player.side, joyconOrientation = player.orientation,
             playerPtr = &player, &mouseConfig]
            (GattCharacteristic const&, GattValueChangedEventArgs const& args)
        {
            // Boost BLE callback thread priority once for lower input latency
            thread_local bool prioritySet = false;
            if (!prioritySet) {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
                prioritySet = true;
            }
            auto reader = DataReader::FromBuffer(args.CharacteristicValue());
            std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
            reader.ReadBytes(buffer);

            // Mouse mode (Right JoyCon only)
            if (joyconSide == JoyConSide::Right && mouseConfig.chatKeyEnabled) {
                uint32_t btnState = ExtractButtonState(buffer);
                bool chatPressed = (btnState & 0x000040) != 0;

                if (chatPressed && !playerPtr->wasChatPressed) {
                    playerPtr->mouseMode = (playerPtr->mouseMode + 1) % 4;
                    uint8_t ledPattern = 0x01;
                    if (playerPtr->mouseMode == 1) ledPattern = 0x02;
                    else if (playerPtr->mouseMode == 2) ledPattern = 0x04;
                    else if (playerPtr->mouseMode == 3) ledPattern = 0x08;
                    SetPlayerLEDsAsync(playerPtr->joycon.writeChar, ledPattern);
                    EmitSoundAsync(playerPtr->joycon.writeChar);
                }
                playerPtr->wasChatPressed = chatPressed;

                if (playerPtr->mouseMode > 0) {
                    playerPtr->mouseInterpolActive.store(true, std::memory_order_relaxed);

                    // Optical mouse movement
                    auto [rawX, rawY] = GetRawOpticalMouse(buffer);
                    if (playerPtr->firstOpticalRead) {
                        playerPtr->lastOpticalX = rawX;
                        playerPtr->lastOpticalY = rawY;
                        playerPtr->firstOpticalRead = false;
                    } else {
                        int16_t dx = rawX - playerPtr->lastOpticalX;
                        int16_t dy = rawY - playerPtr->lastOpticalY;
                        playerPtr->lastOpticalX = rawX;
                        playerPtr->lastOpticalY = rawY;

                        {
                            float sensitivity = mouseConfig.fastSensitivity;
                            if (playerPtr->mouseMode == 2) sensitivity = mouseConfig.normalSensitivity;
                            else if (playerPtr->mouseMode == 3) sensitivity = mouseConfig.slowSensitivity;

                            float scaledDX = dx * sensitivity;
                            float scaledDY = dy * sensitivity;

                            if (mouseConfig.interpolationEnabled) {
                                // Update BLE report interval estimate (exponential moving average)
                                auto now = std::chrono::steady_clock::now();
                                if (playerPtr->bleTimestampInitialized) {
                                    float dtMs = std::chrono::duration<float, std::milli>(now - playerPtr->lastBLETimestamp).count();
                                    if (dtMs > 1.0f && dtMs < 100.0f) {
                                        float prev = playerPtr->reportIntervalMs.load(std::memory_order_relaxed);
                                        playerPtr->reportIntervalMs.store(prev * 0.7f + dtMs * 0.3f, std::memory_order_relaxed);
                                    }
                                }
                                playerPtr->lastBLETimestamp = now;
                                playerPtr->bleTimestampInitialized = true;

                                // Feed interpolation thread with new delta (replaces any pending)
                                playerPtr->pendingDX.store(scaledDX, std::memory_order_relaxed);
                                playerPtr->pendingDY.store(scaledDY, std::memory_order_relaxed);
                                playerPtr->newReportReady.store(true, std::memory_order_release);
                            } else if (dx != 0 || dy != 0) {
                                // Direct mode (no interpolation): original behavior
                                playerPtr->accumX += scaledDX;
                                playerPtr->accumY += scaledDY;

                                int moveX = static_cast<int>(playerPtr->accumX);
                                int moveY = static_cast<int>(playerPtr->accumY);

                                if (moveX != 0 || moveY != 0) {
                                    playerPtr->accumX -= moveX;
                                    playerPtr->accumY -= moveY;

                                    INPUT input = {};
                                    input.type = INPUT_MOUSE;
                                    input.mi.dx = moveX;
                                    input.mi.dy = moveY;
                                    input.mi.dwFlags = MOUSEEVENTF_MOVE | 0x2000;
                                    SendInput(1, &input, sizeof(INPUT));
                                }
                            }
                        }
                    }

                    // Mouse buttons
                    bool rPressed = (btnState & 0x004000) != 0;
                    bool zrPressed = (btnState & 0x008000) != 0;
                    bool stickPressed = (btnState & 0x000004) != 0;

                    if (rPressed && !playerPtr->leftBtnPressed) {
                        INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; SendInput(1, &input, sizeof(INPUT));
                    } else if (!rPressed && playerPtr->leftBtnPressed) {
                        INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_LEFTUP; SendInput(1, &input, sizeof(INPUT));
                    }
                    playerPtr->leftBtnPressed = rPressed;

                    if (zrPressed && !playerPtr->rightBtnPressed) {
                        INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; SendInput(1, &input, sizeof(INPUT));
                    } else if (!zrPressed && playerPtr->rightBtnPressed) {
                        INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_RIGHTUP; SendInput(1, &input, sizeof(INPUT));
                    }
                    playerPtr->rightBtnPressed = zrPressed;

                    if (stickPressed && !playerPtr->middleBtnPressed) {
                        INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN; SendInput(1, &input, sizeof(INPUT));
                    } else if (!stickPressed && playerPtr->middleBtnPressed) {
                        INPUT input = {}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP; SendInput(1, &input, sizeof(INPUT));
                    }
                    playerPtr->middleBtnPressed = stickPressed;

                    // Scroll with configurable speed
                    auto stickData = DecodeJoystick(buffer, joyconSide, joyconOrientation);
                    const int SCROLL_DEADZONE = 4000;
                    if (abs(stickData.y) > SCROLL_DEADZONE) {
                        float intensity = (abs(stickData.y) - SCROLL_DEADZONE) / (32767.0f - SCROLL_DEADZONE);
                        float speed = intensity * mouseConfig.scrollSpeed;
                        if (stickData.y > 0) playerPtr->scrollAccumulator -= speed;
                        else playerPtr->scrollAccumulator += speed;

                        if (abs(playerPtr->scrollAccumulator) >= 120.0f) {
                            int clicks = static_cast<int>(playerPtr->scrollAccumulator / 120.0f);
                            playerPtr->scrollAccumulator -= (clicks * 120.0f);
                            INPUT input = {};
                            input.type = INPUT_MOUSE;
                            input.mi.mouseData = clicks * 120;
                            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
                            SendInput(1, &input, sizeof(INPUT));
                        }
                    } else {
                        playerPtr->scrollAccumulator = 0.0f;
                    }

                    // Side buttons
                    const int BUTTON_THRESHOLD = 28000;
                    if (stickData.x < -BUTTON_THRESHOLD) {
                        if (!playerPtr->mb4Pressed) {
                            INPUT input = {}; input.type = INPUT_MOUSE; input.mi.mouseData = XBUTTON1; input.mi.dwFlags = MOUSEEVENTF_XDOWN; SendInput(1, &input, sizeof(INPUT));
                            INPUT input2 = {}; input2.type = INPUT_MOUSE; input2.mi.mouseData = XBUTTON1; input2.mi.dwFlags = MOUSEEVENTF_XUP; SendInput(1, &input2, sizeof(INPUT));
                            playerPtr->mb4Pressed = true;
                        }
                    } else { playerPtr->mb4Pressed = false; }

                    if (stickData.x > BUTTON_THRESHOLD) {
                        if (!playerPtr->mb5Pressed) {
                            INPUT input = {}; input.type = INPUT_MOUSE; input.mi.mouseData = XBUTTON2; input.mi.dwFlags = MOUSEEVENTF_XDOWN; SendInput(1, &input, sizeof(INPUT));
                            INPUT input2 = {}; input2.type = INPUT_MOUSE; input2.mi.mouseData = XBUTTON2; input2.mi.dwFlags = MOUSEEVENTF_XUP; SendInput(1, &input2, sizeof(INPUT));
                            playerPtr->mb5Pressed = true;
                        }
                    } else { playerPtr->mb5Pressed = false; }

                    // Suppress inputs in DS4 report when mouse mode active
                    buffer[4] &= ~0x40;
                    buffer[4] &= ~0x80;
                    buffer[5] &= ~0x04;
                    if (buffer.size() >= 16) {
                        buffer[13] = 0x00;
                        buffer[14] = 0x08;
                        buffer[15] = 0x80;
                    }
                } else {
                    playerPtr->mouseInterpolActive.store(false, std::memory_order_relaxed);
                    playerPtr->firstOpticalRead = true;
                    playerPtr->accumX = 0.0f;
                    playerPtr->accumY = 0.0f;
                    playerPtr->pendingDX.store(0.0f, std::memory_order_relaxed);
                    playerPtr->pendingDY.store(0.0f, std::memory_order_relaxed);
                    playerPtr->newReportReady.store(false, std::memory_order_relaxed);
                    playerPtr->bleTimestampInitialized = false;
                }
            }

            DS4_REPORT_EX report = GenerateDS4Report(buffer, joyconSide, joyconOrientation);
            if (playerPtr->swapABXY) ApplyABXYSwap(report);
            vigem_target_ds4_update_ex(ViGEmManager::Instance().GetClient(), playerPtr->ds4Controller, report);
        });

        auto status = player.joycon.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

        if (player.joycon.writeChar) {
            SendCustomCommands(player.joycon.writeChar);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            SetPlayerLEDs(player.joycon.writeChar, static_cast<uint8_t>(1 << (GetPlayerCount() - 1)));
            EmitSound(player.joycon.writeChar);
        }

        // Start mouse interpolation thread (shared across all single joycons)
        StartMouseInterpolThread();

        return (status == GattCommunicationStatus::Success);
    }

    // Clear pending dual JoyCon state (release BLE references)
    void ClearPendingDual() {
        pendingDualRight = ConnectedJoyCon{};
        pendingDualGyro = GyroSource::Both;
    }

    // Add Dual JoyCon player (needs two separate scans)
    bool AddDualJoyConFirstStep(ConnectedJoyCon rightJoyCon, GyroSource gyroSource) {
        pendingDualRight = rightJoyCon;
        pendingDualGyro = gyroSource;
        if (rightJoyCon.writeChar) {
            SendCustomCommands(rightJoyCon.writeChar);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            SetPlayerLEDs(rightJoyCon.writeChar, 0x01);
            EmitSound(rightJoyCon.writeChar);
        }
        return true;
    }

    bool AddDualJoyConSecondStep(ConnectedJoyCon leftJoyCon) {
        if (leftJoyCon.writeChar) {
            SendCustomCommands(leftJoyCon.writeChar);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            SetPlayerLEDs(leftJoyCon.writeChar, 0x08);
            EmitSound(leftJoyCon.writeChar);
        }

        auto& vigem = ViGEmManager::Instance();
        PVIGEM_TARGET ds4 = vigem.AllocDS4();
        if (!ds4 || !vigem.AddTarget(ds4)) return false;

        auto dp = std::make_unique<DualJoyConPlayer>();
        dp->leftJoyCon = leftJoyCon;
        dp->rightJoyCon = pendingDualRight;
        dp->gyroSource = pendingDualGyro;
        dp->ds4Controller = ds4;
        dp->bleAddress = pendingDualRight.bleAddress;
        dp->swapABXY = ConfigManager::Instance().GetDeviceSettings(dp->bleAddress).swapABXY;
        dp->running.store(true);

        // Register vibration callback for dual JoyCon
        dp->vibCtx = std::make_unique<VibrationContext>();
        dp->vibCtx->isDual = true;
        dp->vibCtx->writeCharLeft = leftJoyCon.writeChar;
        dp->vibCtx->writeCharRight = pendingDualRight.writeChar;
        vigem_target_ds4_register_notification(
            vigem.GetClient(), ds4, DS4VibrationCallback, dp->vibCtx.get());

        dp->leftJoyCon.inputChar.ValueChanged([ptr = dp.get()](GattCharacteristic const&, GattValueChangedEventArgs const& args) {
            auto reader = DataReader::FromBuffer(args.CharacteristicValue());
            auto buf = std::make_shared<std::vector<uint8_t>>(reader.UnconsumedBufferLength());
            reader.ReadBytes(*buf);
            ptr->leftBufferAtomic.store(buf, std::memory_order_release);
            ptr->bufferCV.notify_one();
        });

        dp->leftJoyCon.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

        dp->rightJoyCon.inputChar.ValueChanged([ptr = dp.get()](GattCharacteristic const&, GattValueChangedEventArgs const& args) {
            auto reader = DataReader::FromBuffer(args.CharacteristicValue());
            auto buf = std::make_shared<std::vector<uint8_t>>(reader.UnconsumedBufferLength());
            reader.ReadBytes(*buf);
            ptr->rightBufferAtomic.store(buf, std::memory_order_release);
            ptr->bufferCV.notify_one();
        });

        dp->rightJoyCon.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

        dp->updateThread = std::thread([ptr = dp.get()]() {
            // Elevate merge thread priority for responsive dual JoyCon input
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
            std::shared_ptr<std::vector<uint8_t>> prevLeft, prevRight;
            while (ptr->running.load(std::memory_order_acquire)) {
                {
                    std::unique_lock<std::mutex> lock(ptr->bufferMutex);
                    ptr->bufferCV.wait_for(lock, std::chrono::milliseconds(2));
                }
                auto leftBuf = ptr->leftBufferAtomic.load(std::memory_order_acquire);
                auto rightBuf = ptr->rightBufferAtomic.load(std::memory_order_acquire);
                if (leftBuf->empty() || rightBuf->empty()) continue;
                // Submit update if either side has new data (don't wait for both)
                if (leftBuf == prevLeft && rightBuf == prevRight) continue;
                prevLeft = leftBuf;
                prevRight = rightBuf;
                DS4_REPORT_EX report = GenerateDualJoyConDS4Report(*leftBuf, *rightBuf, ptr->gyroSource);
                if (ptr->swapABXY) ApplyABXYSwap(report);
                vigem_target_ds4_update_ex(ViGEmManager::Instance().GetClient(), ptr->ds4Controller, report);
            }
        });

        dualPlayers.push_back(std::move(dp));
        ClearPendingDual();  // Release extra BLE references so disconnect works for right Joy-Con
        return true;
    }

    // Add Pro Controller or NSO GC
    bool AddProOrGC(ConnectedJoyCon controller, ControllerType type) {
        auto& vigem = ViGEmManager::Instance();
        PVIGEM_TARGET ds4 = vigem.AllocDS4();
        if (!ds4 || !vigem.AddTarget(ds4)) return false;

        if (type == ControllerType::ProController) {
            ConfigManager::Instance().EnsureDefaults();
            ConfigManager::Instance().Save();
        }

        // Create shared swap flag so BLE callback lambda can access it safely
        auto swapFlag = std::make_shared<std::atomic<bool>>(
            ConfigManager::Instance().GetDeviceSettings(controller.bleAddress).swapABXY);

        if (type == ControllerType::ProController) {
            controller.inputChar.ValueChanged([ds4, swapFlag](GattCharacteristic const&, GattValueChangedEventArgs const& args) mutable {
                thread_local bool prioritySet = false;
                if (!prioritySet) { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); prioritySet = true; }
                auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
                reader.ReadBytes(buffer);
                DS4_REPORT_EX report = GenerateProControllerReport(buffer);
                ApplyGLGRMappings(report, buffer);
                if (swapFlag->load(std::memory_order_relaxed)) ApplyABXYSwap(report);
                HandleSpecialProButtons(buffer);
                vigem_target_ds4_update_ex(ViGEmManager::Instance().GetClient(), ds4, report);
            });
        } else {
            controller.inputChar.ValueChanged([ds4, swapFlag](GattCharacteristic const&, GattValueChangedEventArgs const& args) mutable {
                thread_local bool prioritySet = false;
                if (!prioritySet) { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); prioritySet = true; }
                auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
                reader.ReadBytes(buffer);
                DS4_REPORT_EX report = GenerateNSOGCReport(buffer);
                if (swapFlag->load(std::memory_order_relaxed)) ApplyABXYSwap(report);
                vigem_target_ds4_update_ex(ViGEmManager::Instance().GetClient(), ds4, report);
            });
        }

        controller.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

        if (controller.writeChar) {
            SendCustomCommands(controller.writeChar);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            SetPlayerLEDs(controller.writeChar, static_cast<uint8_t>(1 << (GetPlayerCount())));
            EmitSound(controller.writeChar);
        }

        proPlayers.push_back({ controller, ds4, type, nullptr, swapFlag, controller.bleAddress });

        // Register vibration callback for pro/GC controller
        auto& pp = proPlayers.back();
        pp.vibCtx = std::make_unique<VibrationContext>();
        pp.vibCtx->writeChar = controller.writeChar;
        vigem_target_ds4_register_notification(
            vigem.GetClient(), ds4, DS4VibrationCallback, pp.vibCtx.get());

        return true;
    }

    // Remove player by index across all types
    void RemovePlayerByGlobalIndex(int globalIdx) {
        int idx = globalIdx;
        if (idx < (int)singlePlayers.size()) {
            vigem_target_ds4_unregister_notification(singlePlayers[idx].ds4Controller);
            ViGEmManager::Instance().RemoveTarget(singlePlayers[idx].ds4Controller);
            singlePlayers.erase(singlePlayers.begin() + idx);
            return;
        }
        idx -= (int)singlePlayers.size();
        if (idx < (int)dualPlayers.size()) {
            dualPlayers[idx]->running.store(false);
            if (dualPlayers[idx]->updateThread.joinable()) dualPlayers[idx]->updateThread.join();
            vigem_target_ds4_unregister_notification(dualPlayers[idx]->ds4Controller);
            ViGEmManager::Instance().RemoveTarget(dualPlayers[idx]->ds4Controller);
            dualPlayers.erase(dualPlayers.begin() + idx);
            return;
        }
        idx -= (int)dualPlayers.size();
        if (idx < (int)proPlayers.size()) {
            vigem_target_ds4_unregister_notification(proPlayers[idx].ds4Controller);
            ViGEmManager::Instance().RemoveTarget(proPlayers[idx].ds4Controller);
            proPlayers.erase(proPlayers.begin() + idx);
            return;
        }
    }

    void Shutdown() {
        // Stop mouse interpolation thread
        mouseInterpolRunning.store(false);
        if (mouseInterpolThread.joinable()) mouseInterpolThread.join();

        for (auto& dp : dualPlayers) {
            dp->running.store(false);
            if (dp->updateThread.joinable()) dp->updateThread.join();
            vigem_target_ds4_unregister_notification(dp->ds4Controller);
            ViGEmManager::Instance().RemoveTarget(dp->ds4Controller);
        }
        dualPlayers.clear();
        for (auto& sp : singlePlayers) {
            vigem_target_ds4_unregister_notification(sp.ds4Controller);
            ViGEmManager::Instance().RemoveTarget(sp.ds4Controller);
        }
        singlePlayers.clear();
        for (auto& pp : proPlayers) {
            vigem_target_ds4_unregister_notification(pp.ds4Controller);
            ViGEmManager::Instance().RemoveTarget(pp.ds4Controller);
        }
        proPlayers.clear();
    }

    ~PlayerManager() { Shutdown(); }

private:
    PlayerManager() = default;
    std::vector<SingleJoyConPlayer> singlePlayers;
    std::vector<std::unique_ptr<DualJoyConPlayer>> dualPlayers;
    std::vector<ProControllerPlayer> proPlayers;

    // Mouse interpolation thread
    std::thread mouseInterpolThread;
    std::atomic<bool> mouseInterpolRunning{ false };

    void StartMouseInterpolThread() {
        if (mouseInterpolRunning.load()) return; // already running
        mouseInterpolRunning.store(true);
        mouseInterpolThread = std::thread([this]() {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
            auto& mouseConfig = ConfigManager::Instance().config.mouseConfig;

            // Per-player interpolation state (indexed same as singlePlayers)
            struct InterpState {
                float remainX = 0.0f, remainY = 0.0f;
                float accumX = 0.0f, accumY = 0.0f;
                int ticksLeft = 0;
                float perTickX = 0.0f, perTickY = 0.0f;
                std::chrono::steady_clock::time_point lastActivity{};
            };
            std::vector<InterpState> states;

            while (mouseInterpolRunning.load(std::memory_order_relaxed)) {
                int rateHz = mouseConfig.interpolationRateHz;
                if (rateHz < 100) rateHz = 100;
                if (rateHz > 500) rateHz = 500;
                float tickMs = 1000.0f / rateHz;

                // Ensure states vector matches player count
                if (states.size() < singlePlayers.size()) {
                    states.resize(singlePlayers.size());
                }

                auto now = std::chrono::steady_clock::now();

                for (size_t i = 0; i < singlePlayers.size(); ++i) {
                    auto& player = singlePlayers[i];
                    auto& st = states[i];

                    if (!player.mouseInterpolActive.load(std::memory_order_relaxed))
                        continue;

                    // Check for new BLE report
                    if (player.newReportReady.exchange(false, std::memory_order_acquire)) {
                        float dx = player.pendingDX.exchange(0.0f, std::memory_order_relaxed);
                        float dy = player.pendingDY.exchange(0.0f, std::memory_order_relaxed);

                        // Replace old remainder — new report cancels any unfinished old movement
                        // This prevents inertia when the user stops suddenly
                        st.remainX = dx;
                        st.remainY = dy;

                        if (dx == 0.0f && dy == 0.0f) {
                            // Zero movement: immediately stop all interpolation
                            st.ticksLeft = 0;
                            st.perTickX = 0.0f;
                            st.perTickY = 0.0f;
                            st.remainX = 0.0f;
                            st.remainY = 0.0f;
                        } else {
                            // Calculate how many ticks to spread this over
                            float interval = player.reportIntervalMs.load(std::memory_order_relaxed);
                            if (interval < 5.0f) interval = 5.0f;
                            if (interval > 50.0f) interval = 50.0f;
                            int ticks = (std::max)(1, static_cast<int>(interval / tickMs));

                            st.perTickX = st.remainX / ticks;
                            st.perTickY = st.remainY / ticks;
                            st.ticksLeft = ticks;
                        }
                        st.lastActivity = now;
                    }

                    // Emit one interpolation tick
                    if (st.ticksLeft > 0) {
                        st.accumX += st.perTickX;
                        st.accumY += st.perTickY;
                        st.remainX -= st.perTickX;
                        st.remainY -= st.perTickY;
                        st.ticksLeft--;

                        int moveX = static_cast<int>(st.accumX);
                        int moveY = static_cast<int>(st.accumY);
                        if (moveX != 0 || moveY != 0) {
                            st.accumX -= moveX;
                            st.accumY -= moveY;
                            INPUT input = {};
                            input.type = INPUT_MOUSE;
                            input.mi.dx = moveX;
                            input.mi.dy = moveY;
                            input.mi.dwFlags = MOUSEEVENTF_MOVE | 0x2000;
                            SendInput(1, &input, sizeof(INPUT));
                        }

                        // When done distributing, clear any floating point dust
                        if (st.ticksLeft == 0) {
                            // Send any final remainder
                            st.accumX += st.remainX;
                            st.accumY += st.remainY;
                            int finalX = static_cast<int>(st.accumX);
                            int finalY = static_cast<int>(st.accumY);
                            if (finalX != 0 || finalY != 0) {
                                st.accumX -= finalX;
                                st.accumY -= finalY;
                                INPUT input = {};
                                input.type = INPUT_MOUSE;
                                input.mi.dx = finalX;
                                input.mi.dy = finalY;
                                input.mi.dwFlags = MOUSEEVENTF_MOVE | 0x2000;
                                SendInput(1, &input, sizeof(INPUT));
                            }
                            st.remainX = 0.0f;
                            st.remainY = 0.0f;
                        }
                    } else {
                        // Decay any residual accumulation after inactivity (>50ms)
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.lastActivity).count();
                        if (elapsed > 50) {
                            st.accumX = 0.0f;
                            st.accumY = 0.0f;
                            st.remainX = 0.0f;
                            st.remainY = 0.0f;
                        }
                    }
                }

                // Sleep for one tick interval
                std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int>(tickMs * 1000)));
            }
        });
    }

    // Pending dual JoyCon state
    ConnectedJoyCon pendingDualRight;
    GyroSource pendingDualGyro = GyroSource::Both;
};

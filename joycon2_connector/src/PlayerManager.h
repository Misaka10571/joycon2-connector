#pragma once
// PlayerManager - Central management of all connected controllers
#include "DeviceManager.h"
#include "ViGEmManager.h"
#include "BLECommands.h"
#include "ConfigManager.h"
#include "JoyConDecoder.h"
#include "Logger.h"
#include "BluetoothLog.h"
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <algorithm>
#include <Windows.h>

// Vibration callback context passed to ViGEm as UserData
struct VibrationContext {
    GattCharacteristic vibrationChar{ nullptr };
    GattCharacteristic vibrationCharLeft{ nullptr };   // for dual joycon
    GattCharacteristic vibrationCharRight{ nullptr };  // for dual joycon
    VibrationReportKind reportKind = VibrationReportKind::DualMotor;
    bool isDual = false;
    std::atomic<uint8_t> largeMotor{ 0 };
    std::atomic<uint8_t> smallMotor{ 0 };
    std::atomic<bool> workerRunning{ false };
    std::thread workerThread;
    std::mutex workerMutex;
    std::condition_variable workerCV;
    uint8_t sequenceCounter = 0;  // report sequence (lower 4 bits used by HD rumble slots)
    int outputLinkCount = 0;
    inline static std::atomic<int> connectedOutputLinkCount{ 0 };
    static constexpr int BASE_REFRESH_INTERVAL_MS = 36;
};

struct InputCallbackGate {
    std::mutex mutex;
    bool active = true;
};

inline void SendVibrationFrame(VibrationContext& ctx, uint8_t largeMotor, uint8_t smallMotor) {
    uint8_t sequence = ctx.sequenceCounter++;
    if (ctx.isDual) {
        if (ctx.vibrationCharLeft)
            SendVibrationReport(ctx.vibrationCharLeft, VibrationReportKind::SingleMotor,
                largeMotor, smallMotor, sequence);
        if (ctx.vibrationCharRight)
            SendVibrationReport(ctx.vibrationCharRight, VibrationReportKind::SingleMotor,
                largeMotor, smallMotor, sequence);
    } else if (ctx.vibrationChar) {
        SendVibrationReport(ctx.vibrationChar, ctx.reportKind, largeMotor, smallMotor, sequence);
    }
}

inline bool StartVibrationWorker(VibrationContext& ctx) {
    int outputLinkCount = ctx.isDual
        ? static_cast<int>(static_cast<bool>(ctx.vibrationCharLeft)) +
            static_cast<int>(static_cast<bool>(ctx.vibrationCharRight))
        : static_cast<int>(static_cast<bool>(ctx.vibrationChar));
    if (outputLinkCount == 0) return false;
    if (ctx.workerRunning.exchange(true, std::memory_order_acq_rel)) return true;

    ctx.outputLinkCount = outputLinkCount;
    VibrationContext::connectedOutputLinkCount.fetch_add(outputLinkCount, std::memory_order_acq_rel);

    try {
        ctx.workerThread = std::thread([&ctx]() {
        bool outputActive = false;
        uint16_t gcAccumulator = 0;

        while (ctx.workerRunning.load(std::memory_order_acquire)) {
            uint8_t largeMotor = ctx.largeMotor.load(std::memory_order_relaxed);
            uint8_t smallMotor = ctx.smallMotor.load(std::memory_order_relaxed);
            bool requestedActive = largeMotor != 0 || smallMotor != 0;

            if (requestedActive) {
                if (ctx.reportKind == VibrationReportKind::GameCube && !ctx.isDual) {
                    uint8_t magnitude = (std::max)(largeMotor, static_cast<uint8_t>(smallMotor / 2));
                    gcAccumulator = static_cast<uint16_t>(gcAccumulator + magnitude);
                    bool pulseOn = gcAccumulator >= 255;
                    if (pulseOn) gcAccumulator = static_cast<uint16_t>(gcAccumulator - 255);
                    SendGameCubeVibrationReport(ctx.vibrationChar,
                        pulseOn ? GameCubeRumbleState::On : GameCubeRumbleState::Off,
                        ctx.sequenceCounter++);
                } else {
                    SendVibrationFrame(ctx, largeMotor, smallMotor);
                }
                outputActive = true;

                std::unique_lock<std::mutex> lock(ctx.workerMutex);
                // Each packet carries three subframes. Share the packet rate across BLE links
                // while preserving the original number of vibration samples per second.
                int connectedLinks = (std::max)(1,
                    VibrationContext::connectedOutputLinkCount.load(std::memory_order_acquire));
                ctx.workerCV.wait_for(lock,
                    std::chrono::milliseconds(
                        VibrationContext::BASE_REFRESH_INTERVAL_MS * connectedLinks),
                    [&ctx, largeMotor, smallMotor]() {
                        return !ctx.workerRunning.load(std::memory_order_acquire) ||
                            ctx.largeMotor.load(std::memory_order_relaxed) != largeMotor ||
                            ctx.smallMotor.load(std::memory_order_relaxed) != smallMotor;
                    });
                continue;
            }

            if (outputActive) {
                if (ctx.reportKind == VibrationReportKind::GameCube && !ctx.isDual) {
                    SendGameCubeVibrationReport(ctx.vibrationChar, GameCubeRumbleState::Stop,
                        ctx.sequenceCounter++);
                    gcAccumulator = 0;
                } else {
                    SendVibrationFrame(ctx, 0, 0);
                }
                outputActive = false;
            }

            std::unique_lock<std::mutex> lock(ctx.workerMutex);
            ctx.workerCV.wait(lock, [&ctx]() {
                return !ctx.workerRunning.load(std::memory_order_acquire) ||
                    ctx.largeMotor.load(std::memory_order_relaxed) != 0 ||
                    ctx.smallMotor.load(std::memory_order_relaxed) != 0;
            });
        }

        if (outputActive) {
            if (ctx.reportKind == VibrationReportKind::GameCube && !ctx.isDual) {
                SendGameCubeVibrationReport(ctx.vibrationChar, GameCubeRumbleState::Stop,
                    ctx.sequenceCounter++);
            } else {
                SendVibrationFrame(ctx, 0, 0);
            }
        }
        });
    } catch (...) {
        ctx.workerRunning.store(false, std::memory_order_release);
        VibrationContext::connectedOutputLinkCount.fetch_sub(ctx.outputLinkCount,
            std::memory_order_acq_rel);
        ctx.outputLinkCount = 0;
        APP_LOG_ERROR("Failed to start vibration output worker");
        return false;
    }
    return true;
}

inline void StopVibrationWorker(VibrationContext& ctx) {
    ctx.largeMotor.store(0, std::memory_order_relaxed);
    ctx.smallMotor.store(0, std::memory_order_relaxed);
    bool wasRunning = ctx.workerRunning.exchange(false, std::memory_order_acq_rel);
    ctx.workerCV.notify_one();
    if (ctx.workerThread.joinable()) ctx.workerThread.join();
    if (wasRunning && ctx.outputLinkCount != 0) {
        VibrationContext::connectedOutputLinkCount.fetch_sub(ctx.outputLinkCount,
            std::memory_order_acq_rel);
        ctx.outputLinkCount = 0;
    }
}

inline void HandleVibrationOutput(VibrationContext* ctx, UCHAR LargeMotor, UCHAR SmallMotor) {
    if (!ctx) return;

    auto& vibConfig = ConfigManager::Instance().config.vibrationConfig;
    float scaledLarge = vibConfig.enabled ? LargeMotor * vibConfig.intensity : 0.0f;
    float scaledSmall = vibConfig.enabled ? SmallMotor * vibConfig.intensity : 0.0f;
    uint8_t largeMotor = static_cast<uint8_t>((std::min)(scaledLarge, 255.0f));
    uint8_t smallMotor = static_cast<uint8_t>((std::min)(scaledSmall, 255.0f));
    APP_LOG_DEBUG("Vibration request: large=%u small=%u (scaled=%u/%u)",
        static_cast<unsigned>(LargeMotor), static_cast<unsigned>(SmallMotor),
        static_cast<unsigned>(largeMotor), static_cast<unsigned>(smallMotor));
    ctx->largeMotor.store(largeMotor, std::memory_order_relaxed);
    ctx->smallMotor.store(smallMotor, std::memory_order_relaxed);
    ctx->workerCV.notify_one();
}

// ViGEm DS4 vibration notification callback (runs on ViGEm worker thread)
inline VOID CALLBACK DS4VibrationCallback(
    PVIGEM_CLIENT /*Client*/,
    PVIGEM_TARGET /*Target*/,
    UCHAR LargeMotor,
    UCHAR SmallMotor,
    DS4_LIGHTBAR_COLOR /*LightbarColor*/,
    LPVOID UserData)
{
    HandleVibrationOutput(static_cast<VibrationContext*>(UserData), LargeMotor, SmallMotor);
}

// ViGEm Xbox 360 vibration notification callback (runs on ViGEm worker thread)
inline VOID CALLBACK X360VibrationCallback(
    PVIGEM_CLIENT /*Client*/,
    PVIGEM_TARGET /*Target*/,
    UCHAR LargeMotor,
    UCHAR SmallMotor,
    UCHAR /*LedNumber*/,
    LPVOID UserData)
{
    HandleVibrationOutput(static_cast<VibrationContext*>(UserData), LargeMotor, SmallMotor);
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
    GyroSource gyroSource = GyroSource::Right;
};

struct SingleJoyConPlayer {
    ConnectedJoyCon joycon;
    PVIGEM_TARGET ds4Controller = nullptr;
    JoyConSide side;
    JoyConOrientation orientation;
    // Per-device settings
    bool swapABXY = false;
    bool isXboxMode = false;
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
    winrt::event_token inputChangedToken{};
    std::shared_ptr<InputCallbackGate> inputCallbackGate = std::make_shared<InputCallbackGate>();
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
           inputChangedToken(o.inputChangedToken), inputCallbackGate(std::move(o.inputCallbackGate)),
           pendingDX(o.pendingDX.load()), pendingDY(o.pendingDY.load()),
          newReportReady(o.newReportReady.load()), mouseInterpolActive(o.mouseInterpolActive.load()),
          lastBLETimestamp(o.lastBLETimestamp),
          reportIntervalMs(o.reportIntervalMs.load()),
          bleTimestampInitialized(o.bleTimestampInitialized),
          isXboxMode(o.isXboxMode) {}
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
            inputChangedToken = o.inputChangedToken;
            inputCallbackGate = std::move(o.inputCallbackGate);
            pendingDX.store(o.pendingDX.load()); pendingDY.store(o.pendingDY.load());
            newReportReady.store(o.newReportReady.load()); mouseInterpolActive.store(o.mouseInterpolActive.load());
            lastBLETimestamp = o.lastBLETimestamp;
            reportIntervalMs.store(o.reportIntervalMs.load());
            bleTimestampInitialized = o.bleTimestampInitialized;
            isXboxMode = o.isXboxMode;
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
    bool isXboxMode = false;
    uint64_t bleAddress = 0;  // uses right JoyCon's BLE address
    std::atomic<bool> running{ false };
    std::thread updateThread;
    std::atomic<std::shared_ptr<std::vector<uint8_t>>> leftBufferAtomic{ std::make_shared<std::vector<uint8_t>>() };
    std::atomic<std::shared_ptr<std::vector<uint8_t>>> rightBufferAtomic{ std::make_shared<std::vector<uint8_t>>() };
    std::mutex bufferMutex;
    std::condition_variable bufferCV;
    std::unique_ptr<VibrationContext> vibCtx;
    winrt::event_token leftInputChangedToken{};
    winrt::event_token rightInputChangedToken{};
    std::shared_ptr<InputCallbackGate> inputCallbackGate = std::make_shared<InputCallbackGate>();
};

struct ProControllerPlayer {
    ConnectedJoyCon controller;
    PVIGEM_TARGET ds4Controller = nullptr;
    ControllerType type = ControllerType::ProController; // can also be NSOGCController
    std::unique_ptr<VibrationContext> vibCtx;
    // Per-device settings
    std::shared_ptr<std::atomic<bool>> swapABXYFlag = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> isXboxModeFlag = std::make_shared<std::atomic<bool>>(false);
    uint64_t bleAddress = 0;
    winrt::event_token inputChangedToken{};
    std::shared_ptr<InputCallbackGate> inputCallbackGate = std::make_shared<InputCallbackGate>();
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

// Xbox 360 (XUSB) button mapping application
inline void ApplyButtonMappingXUSB(XUSB_REPORT& report, ButtonMapping mapping) {
    switch (mapping) {
    case ButtonMapping::L3:     report.wButtons |= XUSB_GAMEPAD_LEFT_THUMB; break;
    case ButtonMapping::R3:     report.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB; break;
    case ButtonMapping::L1:     report.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER; break;
    case ButtonMapping::R1:     report.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER; break;
    case ButtonMapping::L2:     report.bLeftTrigger = 255; break;
    case ButtonMapping::R2:     report.bRightTrigger = 255; break;
    case ButtonMapping::CROSS:  report.wButtons |= XUSB_GAMEPAD_A; break;
    case ButtonMapping::CIRCLE: report.wButtons |= XUSB_GAMEPAD_B; break;
    case ButtonMapping::SQUARE: report.wButtons |= XUSB_GAMEPAD_X; break;
    case ButtonMapping::TRIANGLE: report.wButtons |= XUSB_GAMEPAD_Y; break;
    case ButtonMapping::SHARE:  report.wButtons |= XUSB_GAMEPAD_BACK; break;
    case ButtonMapping::OPTIONS:report.wButtons |= XUSB_GAMEPAD_START; break;
    case ButtonMapping::DPAD_UP:    report.wButtons |= XUSB_GAMEPAD_DPAD_UP; break;
    case ButtonMapping::DPAD_DOWN:  report.wButtons |= XUSB_GAMEPAD_DPAD_DOWN; break;
    case ButtonMapping::DPAD_LEFT:  report.wButtons |= XUSB_GAMEPAD_DPAD_LEFT; break;
    case ButtonMapping::DPAD_RIGHT: report.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT; break;
    default: break;
    }
}

// Optical mouse coordinates are absolute 16-bit values that wrap around.
// Convert two consecutive readings to a signed delta without 0xFFFF jump artifacts.
inline int16_t OpticalMouseDelta(int16_t current, int16_t previous) {
    int32_t delta = static_cast<int32_t>(current) - static_cast<int32_t>(previous);
    if (delta > 32767) delta -= 65536;
    else if (delta < -32768) delta += 65536;
    return static_cast<int16_t>(delta);
}

// GL/GR application for Pro controllers (Xbox 360 mode)
inline void ApplyGLGRMappingsXUSB(XUSB_REPORT& report, const std::vector<uint8_t>& buffer) {
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

    if (state & BUTTON_GL_MASK) ApplyButtonMappingXUSB(report, activeLayout.glMapping);
    if (state & BUTTON_GR_MASK) ApplyButtonMappingXUSB(report, activeLayout.grMapping);
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
    constexpr uint64_t BUTTON_SCREENSHOT_MASK = 0x000020000000;
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
    constexpr uint64_t BUTTON_C_MASK = 0x000040000000;
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
    std::vector<std::unique_ptr<SingleJoyConPlayer>>& GetSinglePlayers() { return singlePlayers; }
    std::vector<std::unique_ptr<DualJoyConPlayer>>& GetDualPlayers() { return dualPlayers; }
    std::vector<ProControllerPlayer>& GetProPlayers() { return proPlayers; }

    // Add a Single JoyCon player from async scan result
    bool AddSingleJoyCon(ConnectedJoyCon cj, JoyConSide side, JoyConOrientation orientation,
                         ConnectionError* error = nullptr) {
        std::lock_guard<std::mutex> operationLock(playerOperationsMutex);
        if (error) *error = {};
        if (shuttingDown.load(std::memory_order_acquire))
            return ReportConnectionError(error, ConnectionErrorKind::Internal,
                ConnectionErrorStage::StorePlayer);

        APP_LOG_INFO("Adding single Joy-Con (address: %llu, side: %d, orientation: %d)",
                     cj.bleAddress, static_cast<int>(side), static_cast<int>(orientation));
        if (!cj.inputChar) {
            APP_LOG_ERROR("Single Joy-Con has no input-report characteristic (address: %llu); cannot subscribe",
                          cj.bleAddress);
            return ReportConnectionError(error, ConnectionErrorKind::UnsupportedController,
                ConnectionErrorStage::RegisterInput);
        }
        if (!cj.writeChar) {
            APP_LOG_WARNING("Single Joy-Con has no write-command characteristic (address: %llu); "
                            "LED and command features will be unavailable",
                            cj.bleAddress);
        }

        auto& vigem = ViGEmManager::Instance();
        bool xboxMode = ConfigManager::Instance().GetDeviceSettings(cj.bleAddress).useXboxEmulation;

        PVIGEM_TARGET target = xboxMode ? vigem.AllocX360() : vigem.AllocDS4();
        if (!target) {
            APP_LOG_ERROR("Failed to allocate/add ViGEm target for single Joy-Con");
            return ReportConnectionError(error, ConnectionErrorKind::VirtualControllerUnknown,
                ConnectionErrorStage::CreateVirtualController, "ViGEm target allocation failed");
        }
        VIGEM_ERROR vigemError = vigem.AddTarget(target);
        if (!VIGEM_SUCCESS(vigemError)) {
            std::string detail = ViGEmManager::DescribeError(vigemError);
            APP_LOG_ERROR("Failed to add ViGEm target for single Joy-Con: %s", detail.c_str());
            vigem_target_free(target);
            return ReportConnectionError(error,
                ViGEmManager::HasSuggestedSolution(vigemError)
                    ? ConnectionErrorKind::VirtualController
                    : ConnectionErrorKind::VirtualControllerUnknown,
                ConnectionErrorStage::CreateVirtualController, std::move(detail));
        }

        auto newPlayer = std::make_unique<SingleJoyConPlayer>(cj, target, side, orientation);
        auto& player = *newPlayer;
        player.bleAddress = cj.bleAddress;
        player.swapABXY = ConfigManager::Instance().GetDeviceSettings(cj.bleAddress).swapABXY;
        player.isXboxMode = xboxMode;
        auto& mouseConfig = ConfigManager::Instance().config.mouseConfig;

        // Keep the callback context heap allocated for the lifetime of the ViGEm target.
        player.vibCtx = std::make_unique<VibrationContext>();
        player.vibCtx->vibrationChar = cj.vibrationChar;
        player.vibCtx->reportKind = VibrationReportKind::SingleMotor;

        try {
            player.inputChangedToken = player.joycon.inputChar.ValueChanged(
                [joyconSide = player.side, joyconOrientation = player.orientation,
                 playerPtr = &player, callbackGate = player.inputCallbackGate, &mouseConfig]
                (GattCharacteristic const&, GattValueChangedEventArgs const& args)
            {
                std::lock_guard<std::mutex> callbackLock(callbackGate->mutex);
                if (!callbackGate->active) return;

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
                    APP_LOG_DEBUG("Single Joy-Con CHAT button pressed; mouse mode changed to %d", playerPtr->mouseMode);
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
                        int16_t dx = OpticalMouseDelta(rawX, playerPtr->lastOpticalX);
                        int16_t dy = OpticalMouseDelta(rawY, playerPtr->lastOpticalY);
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
                                    input.mi.dwFlags = MOUSEEVENTF_MOVE;
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

            if (playerPtr->isXboxMode) {
                XUSB_REPORT xreport = GenerateXUSBReport(buffer, joyconSide, joyconOrientation);
                if (playerPtr->swapABXY) ApplyABXYSwapXUSB(xreport);
                vigem_target_x360_update(ViGEmManager::Instance().GetClient(), playerPtr->ds4Controller, xreport);
            } else {
                DS4_REPORT_EX report = GenerateDS4Report(buffer, joyconSide, joyconOrientation);
                ApplyGyroSensitivity(report, ConfigManager::Instance().config.gyroSensitivity);
                if (playerPtr->swapABXY) ApplyABXYSwap(report);
                vigem_target_ds4_update_ex(ViGEmManager::Instance().GetClient(), playerPtr->ds4Controller, report);
            }
            });
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("Single Joy-Con input callback registration threw (address: %llu): %s",
                          cj.bleAddress, BluetoothLog::DescribeHResultError(e).c_str());
            if (xboxMode)
                vigem_target_x360_unregister_notification(target);
            else
                vigem_target_ds4_unregister_notification(target);
            vigem.RemoveTarget(target);
            return ReportConnectionError(error, ConnectionErrorKind::NotificationSubscription,
                ConnectionErrorStage::RegisterInput, BluetoothLog::DescribeHResultError(e));
        } catch (...) {
            APP_LOG_ERROR("Single Joy-Con input callback registration threw an unknown exception (address: %llu)",
                          cj.bleAddress);
            if (xboxMode)
                vigem_target_x360_unregister_notification(target);
            else
                vigem_target_ds4_unregister_notification(target);
            vigem.RemoveTarget(target);
            return ReportConnectionError(error, ConnectionErrorKind::Internal,
                ConnectionErrorStage::RegisterInput);
        }

        GattCommunicationStatus status = GattCommunicationStatus::Success;
        std::string subscribeDetail;
        try {
            status = player.joycon.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("Single Joy-Con input notification subscription threw (address: %llu): %s",
                          cj.bleAddress, BluetoothLog::DescribeHResultError(e).c_str());
            status = GattCommunicationStatus::Unreachable;
            subscribeDetail = BluetoothLog::DescribeHResultError(e);
        } catch (...) {
            APP_LOG_ERROR("Single Joy-Con input notification subscription threw an unknown exception (address: %llu)",
                          cj.bleAddress);
            status = GattCommunicationStatus::Unreachable;
        }

        if (status == GattCommunicationStatus::Success) {
            APP_LOG_INFO("Single Joy-Con input notification subscription succeeded (address: %llu)",
                         cj.bleAddress);
        } else {
            APP_LOG_ERROR("Single Joy-Con input notification subscription failed (address: %llu): %s",
                          cj.bleAddress, BluetoothLog::DescribeGattStatus(status).c_str());
            DisableInputCallback(player.joycon.inputChar,
                                 player.inputChangedToken,
                                 player.inputCallbackGate);
            if (xboxMode)
                vigem_target_x360_unregister_notification(target);
            else
                vigem_target_ds4_unregister_notification(target);
            vigem.RemoveTarget(target);
            return ReportConnectionError(error,
                status == GattCommunicationStatus::AccessDenied
                    ? ConnectionErrorKind::GattAccessDenied
                    : ConnectionErrorKind::NotificationSubscription,
                ConnectionErrorStage::SubscribeNotifications,
                subscribeDetail.empty() ? BluetoothLog::DescribeGattStatus(status)
                                        : std::move(subscribeDetail));
        }

        StartVibrationWorker(*player.vibCtx);
        if (xboxMode) {
            vigem_target_x360_register_notification(
                vigem.GetClient(), target, X360VibrationCallback, player.vibCtx.get());
        } else {
            vigem_target_ds4_register_notification(
                vigem.GetClient(), target, DS4VibrationCallback, player.vibCtx.get());
        }

        if (player.joycon.writeChar) {
            SendCustomCommands(player.joycon.writeChar, 0x37, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            SetPlayerLEDs(player.joycon.writeChar, static_cast<uint8_t>(1 << GetPlayerCount()));
            EmitSound(player.joycon.writeChar);
        }

        {
            try {
                std::lock_guard<std::mutex> playersLock(singlePlayersMutex);
                singlePlayers.push_back(std::move(newPlayer));
            } catch (...) {
                DisableInputCallback(player.joycon.inputChar,
                                     player.inputChangedToken,
                                     player.inputCallbackGate);
                if (xboxMode)
                    vigem_target_x360_unregister_notification(target);
                else
                    vigem_target_ds4_unregister_notification(target);
                StopVibrationWorker(*player.vibCtx);
                vigem.RemoveTarget(target);
                APP_LOG_ERROR("Failed to store single Joy-Con player (address: %llu)", cj.bleAddress);
                return ReportConnectionError(error, ConnectionErrorKind::Internal,
                    ConnectionErrorStage::StorePlayer);
            }
        }

        // Start mouse interpolation thread (shared across all single joycons)
        StartMouseInterpolThread();

        APP_LOG_INFO("Single Joy-Con add completed (success: %d, total players: %d)",
                     (status == GattCommunicationStatus::Success) ? 1 : 0, GetPlayerCount());
        return true;
    }

    // Clear pending dual JoyCon state (release BLE references)
    void ClearPendingDual() {
        std::lock_guard<std::mutex> operationLock(playerOperationsMutex);
        ClearPendingDualUnlocked();
    }

    // Add Dual JoyCon player (needs two separate scans)
    bool AddDualJoyConFirstStep(ConnectedJoyCon rightJoyCon, GyroSource gyroSource,
                               ConnectionError* error = nullptr) {
        std::lock_guard<std::mutex> operationLock(playerOperationsMutex);
        if (error) *error = {};
        if (shuttingDown.load(std::memory_order_acquire))
            return ReportConnectionError(error, ConnectionErrorKind::Internal,
                ConnectionErrorStage::StorePlayer);

        APP_LOG_INFO("Dual Joy-Con first step: right Joy-Con paired (address: %llu, gyro: %d)",
                     rightJoyCon.bleAddress, static_cast<int>(gyroSource));
        if (!rightJoyCon.inputChar) {
            APP_LOG_ERROR("Right Joy-Con has no input-report characteristic (address: %llu); cannot subscribe",
                          rightJoyCon.bleAddress);
            return ReportConnectionError(error, ConnectionErrorKind::UnsupportedController,
                ConnectionErrorStage::RegisterInput);
        }
        if (!rightJoyCon.writeChar) {
            APP_LOG_WARNING("Right Joy-Con has no write-command characteristic (address: %llu); "
                            "LED and command features will be unavailable",
                            rightJoyCon.bleAddress);
        }

        pendingDualRight = rightJoyCon;
        pendingDualGyro = gyroSource;
        if (rightJoyCon.writeChar) {
            SendCustomCommands(rightJoyCon.writeChar, 0x37, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            SetPlayerLEDs(rightJoyCon.writeChar, 0x01);
            EmitSound(rightJoyCon.writeChar);
        }
        return true;
    }

    bool AddDualJoyConSecondStep(ConnectedJoyCon leftJoyCon, ConnectionError* error = nullptr) {
        std::lock_guard<std::mutex> operationLock(playerOperationsMutex);
        if (error) *error = {};
        if (shuttingDown.load(std::memory_order_acquire))
            return ReportConnectionError(error, ConnectionErrorKind::Internal,
                ConnectionErrorStage::StorePlayer);

        APP_LOG_INFO("Dual Joy-Con second step: left Joy-Con paired (address: %llu)", leftJoyCon.bleAddress);
        if (!leftJoyCon.inputChar) {
            APP_LOG_ERROR("Left Joy-Con has no input-report characteristic (address: %llu); cannot subscribe",
                          leftJoyCon.bleAddress);
            ClearPendingDualUnlocked();
            return ReportConnectionError(error, ConnectionErrorKind::UnsupportedController,
                ConnectionErrorStage::RegisterInput);
        }
        if (!pendingDualRight.inputChar) {
            APP_LOG_ERROR("Pending right Joy-Con has no input-report characteristic (address: %llu); cannot subscribe",
                          pendingDualRight.bleAddress);
            ClearPendingDualUnlocked();
            return ReportConnectionError(error, ConnectionErrorKind::UnsupportedController,
                ConnectionErrorStage::RegisterInput);
        }
        if (leftJoyCon.writeChar) {
            SendCustomCommands(leftJoyCon.writeChar, 0x37, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            SetPlayerLEDs(leftJoyCon.writeChar, 0x08);
            EmitSound(leftJoyCon.writeChar);
        }

        auto& vigem = ViGEmManager::Instance();
        bool xboxMode = ConfigManager::Instance().GetDeviceSettings(pendingDualRight.bleAddress).useXboxEmulation;

        PVIGEM_TARGET target = xboxMode ? vigem.AllocX360() : vigem.AllocDS4();
        if (!target) {
            APP_LOG_ERROR("Failed to allocate/add ViGEm target for dual Joy-Con");
            return ReportConnectionError(error, ConnectionErrorKind::VirtualControllerUnknown,
                ConnectionErrorStage::CreateVirtualController, "ViGEm target allocation failed");
        }
        VIGEM_ERROR vigemError = vigem.AddTarget(target);
        if (!VIGEM_SUCCESS(vigemError)) {
            std::string detail = ViGEmManager::DescribeError(vigemError);
            APP_LOG_ERROR("Failed to add ViGEm target for dual Joy-Con: %s", detail.c_str());
            vigem_target_free(target);
            return ReportConnectionError(error,
                ViGEmManager::HasSuggestedSolution(vigemError)
                    ? ConnectionErrorKind::VirtualController
                    : ConnectionErrorKind::VirtualControllerUnknown,
                ConnectionErrorStage::CreateVirtualController, std::move(detail));
        }

        auto dp = std::make_unique<DualJoyConPlayer>();
        dp->leftJoyCon = leftJoyCon;
        dp->rightJoyCon = pendingDualRight;
        dp->gyroSource = pendingDualGyro;
        dp->ds4Controller = target;
        dp->bleAddress = pendingDualRight.bleAddress;
        dp->swapABXY = ConfigManager::Instance().GetDeviceSettings(dp->bleAddress).swapABXY;
        dp->isXboxMode = xboxMode;
        dp->running.store(true);

        // Keep the callback context stable while both controller objects are active.
        dp->vibCtx = std::make_unique<VibrationContext>();
        dp->vibCtx->isDual = true;
        dp->vibCtx->reportKind = VibrationReportKind::SingleMotor;
        dp->vibCtx->vibrationCharLeft = leftJoyCon.vibrationChar;
        dp->vibCtx->vibrationCharRight = pendingDualRight.vibrationChar;

        try {
            dp->leftInputChangedToken = dp->leftJoyCon.inputChar.ValueChanged(
                [ptr = dp.get(), callbackGate = dp->inputCallbackGate]
                (GattCharacteristic const&, GattValueChangedEventArgs const& args) {
                std::lock_guard<std::mutex> callbackLock(callbackGate->mutex);
                if (!callbackGate->active) return;
                auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                auto buf = std::make_shared<std::vector<uint8_t>>(reader.UnconsumedBufferLength());
                reader.ReadBytes(*buf);
                ptr->leftBufferAtomic.store(buf, std::memory_order_release);
                ptr->bufferCV.notify_one();
            });
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("Dual Joy-Con left input callback registration threw: %s",
                          BluetoothLog::DescribeHResultError(e).c_str());
            CleanupUnpublishedDual(*dp);
            ClearPendingDualUnlocked();
            return ReportConnectionError(error, ConnectionErrorKind::NotificationSubscription,
                ConnectionErrorStage::RegisterInput, BluetoothLog::DescribeHResultError(e));
        } catch (...) {
            APP_LOG_ERROR("Dual Joy-Con left input callback registration threw an unknown exception");
            CleanupUnpublishedDual(*dp);
            ClearPendingDualUnlocked();
            return ReportConnectionError(error, ConnectionErrorKind::Internal,
                ConnectionErrorStage::RegisterInput);
        }

        GattCommunicationStatus leftSubscribeStatus = GattCommunicationStatus::Success;
        std::string leftSubscribeDetail;
        try {
            leftSubscribeStatus = dp->leftJoyCon.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("Dual Joy-Con left notification subscription threw (address: %llu): %s",
                          leftJoyCon.bleAddress, BluetoothLog::DescribeHResultError(e).c_str());
            leftSubscribeStatus = GattCommunicationStatus::Unreachable;
            leftSubscribeDetail = BluetoothLog::DescribeHResultError(e);
        } catch (...) {
            APP_LOG_ERROR("Dual Joy-Con left notification subscription threw an unknown exception (address: %llu)",
                          leftJoyCon.bleAddress);
            leftSubscribeStatus = GattCommunicationStatus::Unreachable;
        }

        if (leftSubscribeStatus == GattCommunicationStatus::Success) {
            APP_LOG_INFO("Dual Joy-Con left input notification subscription succeeded (address: %llu)",
                         leftJoyCon.bleAddress);
        } else {
            APP_LOG_ERROR("Dual Joy-Con left input notification subscription failed (address: %llu): %s",
                          leftJoyCon.bleAddress,
                          BluetoothLog::DescribeGattStatus(leftSubscribeStatus).c_str());
            CleanupUnpublishedDual(*dp);
            ClearPendingDualUnlocked();
            return ReportConnectionError(error,
                leftSubscribeStatus == GattCommunicationStatus::AccessDenied
                    ? ConnectionErrorKind::GattAccessDenied
                    : ConnectionErrorKind::NotificationSubscription,
                ConnectionErrorStage::SubscribeNotifications,
                leftSubscribeDetail.empty() ? BluetoothLog::DescribeGattStatus(leftSubscribeStatus)
                                            : std::move(leftSubscribeDetail));
        }

        try {
            dp->rightInputChangedToken = dp->rightJoyCon.inputChar.ValueChanged(
                [ptr = dp.get(), callbackGate = dp->inputCallbackGate]
                (GattCharacteristic const&, GattValueChangedEventArgs const& args) {
                std::lock_guard<std::mutex> callbackLock(callbackGate->mutex);
                if (!callbackGate->active) return;
                auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                auto buf = std::make_shared<std::vector<uint8_t>>(reader.UnconsumedBufferLength());
                reader.ReadBytes(*buf);
                ptr->rightBufferAtomic.store(buf, std::memory_order_release);
                ptr->bufferCV.notify_one();
            });
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("Dual Joy-Con right input callback registration threw: %s",
                          BluetoothLog::DescribeHResultError(e).c_str());
            CleanupUnpublishedDual(*dp);
            ClearPendingDualUnlocked();
            return ReportConnectionError(error, ConnectionErrorKind::NotificationSubscription,
                ConnectionErrorStage::RegisterInput, BluetoothLog::DescribeHResultError(e));
        } catch (...) {
            APP_LOG_ERROR("Dual Joy-Con right input callback registration threw an unknown exception");
            CleanupUnpublishedDual(*dp);
            ClearPendingDualUnlocked();
            return ReportConnectionError(error, ConnectionErrorKind::Internal,
                ConnectionErrorStage::RegisterInput);
        }

        GattCommunicationStatus rightSubscribeStatus = GattCommunicationStatus::Success;
        std::string rightSubscribeDetail;
        try {
            rightSubscribeStatus = dp->rightJoyCon.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("Dual Joy-Con right notification subscription threw (address: %llu): %s",
                          pendingDualRight.bleAddress, BluetoothLog::DescribeHResultError(e).c_str());
            rightSubscribeStatus = GattCommunicationStatus::Unreachable;
            rightSubscribeDetail = BluetoothLog::DescribeHResultError(e);
        } catch (...) {
            APP_LOG_ERROR("Dual Joy-Con right notification subscription threw an unknown exception (address: %llu)",
                          pendingDualRight.bleAddress);
            rightSubscribeStatus = GattCommunicationStatus::Unreachable;
        }

        if (rightSubscribeStatus == GattCommunicationStatus::Success) {
            APP_LOG_INFO("Dual Joy-Con right input notification subscription succeeded (address: %llu)",
                         pendingDualRight.bleAddress);
        } else {
            APP_LOG_ERROR("Dual Joy-Con right input notification subscription failed (address: %llu): %s",
                          pendingDualRight.bleAddress,
                          BluetoothLog::DescribeGattStatus(rightSubscribeStatus).c_str());
            CleanupUnpublishedDual(*dp);
            ClearPendingDualUnlocked();
            return ReportConnectionError(error,
                rightSubscribeStatus == GattCommunicationStatus::AccessDenied
                    ? ConnectionErrorKind::GattAccessDenied
                    : ConnectionErrorKind::NotificationSubscription,
                ConnectionErrorStage::SubscribeNotifications,
                rightSubscribeDetail.empty() ? BluetoothLog::DescribeGattStatus(rightSubscribeStatus)
                                             : std::move(rightSubscribeDetail));
        }

        try {
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
                    if (ptr->isXboxMode) {
                        XUSB_REPORT xreport = GenerateDualJoyConXUSBReport(*leftBuf, *rightBuf);
                        if (ptr->swapABXY) ApplyABXYSwapXUSB(xreport);
                        vigem_target_x360_update(ViGEmManager::Instance().GetClient(), ptr->ds4Controller, xreport);
                    } else {
                        DS4_REPORT_EX report = GenerateDualJoyConDS4Report(*leftBuf, *rightBuf, ptr->gyroSource);
                        ApplyGyroSensitivity(report, ConfigManager::Instance().config.gyroSensitivity);
                        if (ptr->swapABXY) ApplyABXYSwap(report);
                        vigem_target_ds4_update_ex(ViGEmManager::Instance().GetClient(), ptr->ds4Controller, report);
                    }
                }
            });
        } catch (...) {
            APP_LOG_ERROR("Failed to start dual Joy-Con update thread");
            CleanupUnpublishedDual(*dp);
            ClearPendingDualUnlocked();
            return ReportConnectionError(error, ConnectionErrorKind::Internal,
                ConnectionErrorStage::StartWorker);
        }

        StartVibrationWorker(*dp->vibCtx);
        if (xboxMode) {
            vigem_target_x360_register_notification(
                vigem.GetClient(), target, X360VibrationCallback, dp->vibCtx.get());
        } else {
            vigem_target_ds4_register_notification(
                vigem.GetClient(), target, DS4VibrationCallback, dp->vibCtx.get());
        }

        try {
            dualPlayers.push_back(std::move(dp));
        } catch (...) {
            CleanupUnpublishedDual(*dp);
            ClearPendingDualUnlocked();
            APP_LOG_ERROR("Failed to store dual Joy-Con player");
            return ReportConnectionError(error, ConnectionErrorKind::Internal,
                ConnectionErrorStage::StorePlayer);
        }
        ClearPendingDualUnlocked();  // Release extra BLE references so disconnect works for right Joy-Con
        APP_LOG_INFO("Dual Joy-Con add completed (total players: %d)", GetPlayerCount());
        return true;
    }

    // Add Pro Controller or NSO GC
    bool AddProOrGC(ConnectedJoyCon controller, ControllerType type,
                    ConnectionError* error = nullptr) {
        std::lock_guard<std::mutex> operationLock(playerOperationsMutex);
        if (error) *error = {};
        if (shuttingDown.load(std::memory_order_acquire))
            return ReportConnectionError(error, ConnectionErrorKind::Internal,
                ConnectionErrorStage::StorePlayer);

        APP_LOG_INFO("Adding Pro/NSO-GC controller (address: %llu, type: %d)",
                     controller.bleAddress, static_cast<int>(type));
        if (!controller.inputChar) {
            APP_LOG_ERROR("Pro/NSO-GC controller has no input-report characteristic (address: %llu); cannot subscribe",
                          controller.bleAddress);
            return ReportConnectionError(error, ConnectionErrorKind::UnsupportedController,
                ConnectionErrorStage::RegisterInput);
        }
        if (!controller.writeChar) {
            APP_LOG_WARNING("Pro/NSO-GC controller has no write-command characteristic (address: %llu); "
                            "LED and command features will be unavailable",
                            controller.bleAddress);
        }

        auto& vigem = ViGEmManager::Instance();
        bool xboxMode = ConfigManager::Instance().GetDeviceSettings(controller.bleAddress).useXboxEmulation;

        PVIGEM_TARGET target = xboxMode ? vigem.AllocX360() : vigem.AllocDS4();
        if (!target) {
            APP_LOG_ERROR("Failed to allocate/add ViGEm target for Pro/NSO-GC controller");
            return ReportConnectionError(error, ConnectionErrorKind::VirtualControllerUnknown,
                ConnectionErrorStage::CreateVirtualController, "ViGEm target allocation failed");
        }
        VIGEM_ERROR vigemError = vigem.AddTarget(target);
        if (!VIGEM_SUCCESS(vigemError)) {
            std::string detail = ViGEmManager::DescribeError(vigemError);
            APP_LOG_ERROR("Failed to add ViGEm target for Pro/NSO-GC controller: %s", detail.c_str());
            vigem_target_free(target);
            return ReportConnectionError(error,
                ViGEmManager::HasSuggestedSolution(vigemError)
                    ? ConnectionErrorKind::VirtualController
                    : ConnectionErrorKind::VirtualControllerUnknown,
                ConnectionErrorStage::CreateVirtualController, std::move(detail));
        }

        if (type == ControllerType::ProController) {
            ConfigManager::Instance().EnsureDefaults();
            ConfigManager::Instance().Save();
        }

        // Create shared flags so BLE callback lambda can access them safely
        auto swapFlag = std::make_shared<std::atomic<bool>>(
            ConfigManager::Instance().GetDeviceSettings(controller.bleAddress).swapABXY);
        auto xboxModeFlag = std::make_shared<std::atomic<bool>>(xboxMode);
        auto inputCallbackGate = std::make_shared<InputCallbackGate>();
        winrt::event_token inputChangedToken{};

        try {
        if (type == ControllerType::ProController) {
            if (xboxMode) {
                inputChangedToken = controller.inputChar.ValueChanged([target, swapFlag, inputCallbackGate](GattCharacteristic const&, GattValueChangedEventArgs const& args) mutable {
                    std::lock_guard<std::mutex> callbackLock(inputCallbackGate->mutex);
                    if (!inputCallbackGate->active) return;
                    thread_local bool prioritySet = false;
                    if (!prioritySet) { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); prioritySet = true; }
                    auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                    std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
                    reader.ReadBytes(buffer);
                    XUSB_REPORT xreport = GenerateProControllerXUSBReport(buffer);
                    ApplyGLGRMappingsXUSB(xreport, buffer);
                    if (swapFlag->load(std::memory_order_relaxed)) ApplyABXYSwapXUSB(xreport);
                    HandleSpecialProButtons(buffer);
                    vigem_target_x360_update(ViGEmManager::Instance().GetClient(), target, xreport);
                });
            } else {
                inputChangedToken = controller.inputChar.ValueChanged([target, swapFlag, inputCallbackGate](GattCharacteristic const&, GattValueChangedEventArgs const& args) mutable {
                    std::lock_guard<std::mutex> callbackLock(inputCallbackGate->mutex);
                    if (!inputCallbackGate->active) return;
                    thread_local bool prioritySet = false;
                    if (!prioritySet) { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); prioritySet = true; }
                    auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                    std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
                    reader.ReadBytes(buffer);
                    DS4_REPORT_EX report = GenerateProControllerReport(buffer);
                    ApplyGyroSensitivity(report, ConfigManager::Instance().config.gyroSensitivity);
                    ApplyGLGRMappings(report, buffer);
                    if (swapFlag->load(std::memory_order_relaxed)) ApplyABXYSwap(report);
                    HandleSpecialProButtons(buffer);
                    vigem_target_ds4_update_ex(ViGEmManager::Instance().GetClient(), target, report);
                });
            }
        } else {
            if (xboxMode) {
                inputChangedToken = controller.inputChar.ValueChanged([target, swapFlag, inputCallbackGate](GattCharacteristic const&, GattValueChangedEventArgs const& args) mutable {
                    std::lock_guard<std::mutex> callbackLock(inputCallbackGate->mutex);
                    if (!inputCallbackGate->active) return;
                    thread_local bool prioritySet = false;
                    if (!prioritySet) { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); prioritySet = true; }
                    auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                    std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
                    reader.ReadBytes(buffer);
                    XUSB_REPORT xreport = GenerateNSOGCXUSBReport(buffer);
                    if (swapFlag->load(std::memory_order_relaxed)) ApplyABXYSwapXUSB(xreport);
                    vigem_target_x360_update(ViGEmManager::Instance().GetClient(), target, xreport);
                });
            } else {
                inputChangedToken = controller.inputChar.ValueChanged([target, swapFlag, inputCallbackGate](GattCharacteristic const&, GattValueChangedEventArgs const& args) mutable {
                    std::lock_guard<std::mutex> callbackLock(inputCallbackGate->mutex);
                    if (!inputCallbackGate->active) return;
                    thread_local bool prioritySet = false;
                    if (!prioritySet) { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); prioritySet = true; }
                    auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                    std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
                    reader.ReadBytes(buffer);
                    DS4_REPORT_EX report = GenerateNSOGCReport(buffer);
                    ApplyGyroSensitivity(report, ConfigManager::Instance().config.gyroSensitivity);
                    if (swapFlag->load(std::memory_order_relaxed)) ApplyABXYSwap(report);
                    vigem_target_ds4_update_ex(ViGEmManager::Instance().GetClient(), target, report);
                });
            }
        }
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("Pro/NSO-GC input callback registration threw (address: %llu): %s",
                          controller.bleAddress, BluetoothLog::DescribeHResultError(e).c_str());
            vigem.RemoveTarget(target);
            return ReportConnectionError(error, ConnectionErrorKind::NotificationSubscription,
                ConnectionErrorStage::RegisterInput, BluetoothLog::DescribeHResultError(e));
        } catch (...) {
            APP_LOG_ERROR("Pro/NSO-GC input callback registration threw an unknown exception (address: %llu)",
                          controller.bleAddress);
            vigem.RemoveTarget(target);
            return ReportConnectionError(error, ConnectionErrorKind::Internal,
                ConnectionErrorStage::RegisterInput);
        }

        GattCommunicationStatus proStatus = GattCommunicationStatus::Success;
        std::string proSubscribeDetail;
        try {
            proStatus = controller.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("Pro/NSO-GC notification subscription threw (address: %llu): %s",
                          controller.bleAddress, BluetoothLog::DescribeHResultError(e).c_str());
            proStatus = GattCommunicationStatus::Unreachable;
            proSubscribeDetail = BluetoothLog::DescribeHResultError(e);
        } catch (...) {
            APP_LOG_ERROR("Pro/NSO-GC notification subscription threw an unknown exception (address: %llu)",
                          controller.bleAddress);
            proStatus = GattCommunicationStatus::Unreachable;
        }

        if (proStatus == GattCommunicationStatus::Success) {
            APP_LOG_INFO("Pro/NSO-GC input notification subscription succeeded (address: %llu)",
                         controller.bleAddress);
        } else {
            APP_LOG_ERROR("Pro/NSO-GC input notification subscription failed (address: %llu): %s",
                          controller.bleAddress,
                          BluetoothLog::DescribeGattStatus(proStatus).c_str());
            DisableInputCallback(controller.inputChar, inputChangedToken, inputCallbackGate);
            vigem.RemoveTarget(target);
            return ReportConnectionError(error,
                proStatus == GattCommunicationStatus::AccessDenied
                    ? ConnectionErrorKind::GattAccessDenied
                    : ConnectionErrorKind::NotificationSubscription,
                ConnectionErrorStage::SubscribeNotifications,
                proSubscribeDetail.empty() ? BluetoothLog::DescribeGattStatus(proStatus)
                                           : std::move(proSubscribeDetail));
        }

        if (controller.writeChar) {
            uint8_t featureFlags = (type == ControllerType::ProController) ? 0x2F : 0x27;
            SendCustomCommands(controller.writeChar, featureFlags, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            SetPlayerLEDs(controller.writeChar, static_cast<uint8_t>(1 << (GetPlayerCount())));
            EmitSound(controller.writeChar);
        }

        try {
            proPlayers.push_back({ controller, target, type, nullptr, swapFlag, xboxModeFlag, controller.bleAddress });
        } catch (...) {
            DisableInputCallback(controller.inputChar, inputChangedToken, inputCallbackGate);
            vigem.RemoveTarget(target);
            APP_LOG_ERROR("Failed to store Pro/NSO-GC controller player");
            return ReportConnectionError(error, ConnectionErrorKind::Internal,
                ConnectionErrorStage::StorePlayer);
        }

        // Register vibration callback for pro/GC controller
        auto& pp = proPlayers.back();
        pp.inputChangedToken = inputChangedToken;
        pp.inputCallbackGate = inputCallbackGate;
        pp.vibCtx = std::make_unique<VibrationContext>();
        pp.vibCtx->vibrationChar = controller.vibrationChar;
        pp.vibCtx->reportKind = (type == ControllerType::ProController)
            ? VibrationReportKind::DualMotor
            : VibrationReportKind::GameCube;
        StartVibrationWorker(*pp.vibCtx);
        if (xboxMode) {
            vigem_target_x360_register_notification(
                vigem.GetClient(), target, X360VibrationCallback, pp.vibCtx.get());
        } else {
            vigem_target_ds4_register_notification(
                vigem.GetClient(), target, DS4VibrationCallback, pp.vibCtx.get());
        }

        APP_LOG_INFO("Pro/NSO-GC add completed (total players: %d)", GetPlayerCount());
        return true;
    }

    // Remove player by index across all types
    void RemovePlayerByGlobalIndex(int globalIdx) {
        std::lock_guard<std::mutex> operationLock(playerOperationsMutex);
        APP_LOG_INFO("Removing player at global index %d", globalIdx);
        int idx = globalIdx;
        std::unique_lock<std::mutex> singlePlayersLock(singlePlayersMutex);
        if (idx < (int)singlePlayers.size()) {
            DisableInputCallback(singlePlayers[idx]->joycon.inputChar,
                                 singlePlayers[idx]->inputChangedToken,
                                 singlePlayers[idx]->inputCallbackGate);
            if (singlePlayers[idx]->isXboxMode)
                vigem_target_x360_unregister_notification(singlePlayers[idx]->ds4Controller);
            else
                vigem_target_ds4_unregister_notification(singlePlayers[idx]->ds4Controller);
            StopVibrationWorker(*singlePlayers[idx]->vibCtx);
            ViGEmManager::Instance().RemoveTarget(singlePlayers[idx]->ds4Controller);
            singlePlayers.erase(singlePlayers.begin() + idx);
            return;
        }
        idx -= (int)singlePlayers.size();
        singlePlayersLock.unlock();
        if (idx < (int)dualPlayers.size()) {
            dualPlayers[idx]->running.store(false);
            dualPlayers[idx]->bufferCV.notify_one();
            if (dualPlayers[idx]->updateThread.joinable()) dualPlayers[idx]->updateThread.join();
            DisableInputCallback(dualPlayers[idx]->leftJoyCon.inputChar,
                                 dualPlayers[idx]->leftInputChangedToken,
                                 dualPlayers[idx]->inputCallbackGate);
            UnregisterInputCallback(dualPlayers[idx]->rightJoyCon.inputChar,
                                    dualPlayers[idx]->rightInputChangedToken);
            if (dualPlayers[idx]->isXboxMode)
                vigem_target_x360_unregister_notification(dualPlayers[idx]->ds4Controller);
            else
                vigem_target_ds4_unregister_notification(dualPlayers[idx]->ds4Controller);
            StopVibrationWorker(*dualPlayers[idx]->vibCtx);
            ViGEmManager::Instance().RemoveTarget(dualPlayers[idx]->ds4Controller);
            dualPlayers.erase(dualPlayers.begin() + idx);
            return;
        }
        idx -= (int)dualPlayers.size();
        if (idx < (int)proPlayers.size()) {
            DisableInputCallback(proPlayers[idx].controller.inputChar,
                                 proPlayers[idx].inputChangedToken,
                                 proPlayers[idx].inputCallbackGate);
            if (proPlayers[idx].isXboxModeFlag->load(std::memory_order_relaxed))
                vigem_target_x360_unregister_notification(proPlayers[idx].ds4Controller);
            else
                vigem_target_ds4_unregister_notification(proPlayers[idx].ds4Controller);
            StopVibrationWorker(*proPlayers[idx].vibCtx);
            ViGEmManager::Instance().RemoveTarget(proPlayers[idx].ds4Controller);
            proPlayers.erase(proPlayers.begin() + idx);
            return;
        }
    }

    void Shutdown() {
        if (shuttingDown.exchange(true, std::memory_order_acq_rel)) return;
        std::lock_guard<std::mutex> operationLock(playerOperationsMutex);

        APP_LOG_INFO("PlayerManager shutdown: disconnecting %d player(s)", GetPlayerCount());
        // Stop mouse interpolation thread
        mouseInterpolRunning.store(false);
        if (mouseInterpolThread.joinable()) mouseInterpolThread.join();

        for (auto& dp : dualPlayers) {
            dp->running.store(false);
            dp->bufferCV.notify_one();
            if (dp->updateThread.joinable()) dp->updateThread.join();
            DisableInputCallback(dp->leftJoyCon.inputChar,
                                 dp->leftInputChangedToken,
                                 dp->inputCallbackGate);
            UnregisterInputCallback(dp->rightJoyCon.inputChar, dp->rightInputChangedToken);
            if (dp->isXboxMode)
                vigem_target_x360_unregister_notification(dp->ds4Controller);
            else
                vigem_target_ds4_unregister_notification(dp->ds4Controller);
            StopVibrationWorker(*dp->vibCtx);
            ViGEmManager::Instance().RemoveTarget(dp->ds4Controller);
        }
        dualPlayers.clear();
        for (auto& sp : singlePlayers) {
            DisableInputCallback(sp->joycon.inputChar,
                                 sp->inputChangedToken,
                                 sp->inputCallbackGate);
            if (sp->isXboxMode)
                vigem_target_x360_unregister_notification(sp->ds4Controller);
            else
                vigem_target_ds4_unregister_notification(sp->ds4Controller);
            StopVibrationWorker(*sp->vibCtx);
            ViGEmManager::Instance().RemoveTarget(sp->ds4Controller);
        }
        singlePlayers.clear();
        for (auto& pp : proPlayers) {
            DisableInputCallback(pp.controller.inputChar,
                                 pp.inputChangedToken,
                                 pp.inputCallbackGate);
            if (pp.isXboxModeFlag->load(std::memory_order_relaxed))
                vigem_target_x360_unregister_notification(pp.ds4Controller);
            else
                vigem_target_ds4_unregister_notification(pp.ds4Controller);
            StopVibrationWorker(*pp.vibCtx);
            ViGEmManager::Instance().RemoveTarget(pp.ds4Controller);
        }
        proPlayers.clear();
    }

    ~PlayerManager() { Shutdown(); }

private:
    PlayerManager() = default;
    std::vector<std::unique_ptr<SingleJoyConPlayer>> singlePlayers;
    std::vector<std::unique_ptr<DualJoyConPlayer>> dualPlayers;
    std::vector<ProControllerPlayer> proPlayers;

    // Mouse interpolation thread
    std::thread mouseInterpolThread;
    std::atomic<bool> mouseInterpolRunning{ false };
    std::mutex singlePlayersMutex;
    std::mutex playerOperationsMutex;
    std::atomic<bool> shuttingDown{ false };

    static void UnregisterInputCallback(const GattCharacteristic& inputChar,
                                        winrt::event_token token) {
        if (!inputChar || token.value == 0) return;
        try {
            inputChar.ValueChanged(token);
        } catch (const winrt::hresult_error& e) {
            APP_LOG_WARNING("Failed to unregister BLE input callback: %s",
                            BluetoothLog::DescribeHResultError(e).c_str());
        } catch (...) {
            APP_LOG_WARNING("Failed to unregister BLE input callback: unknown exception");
        }
    }

    static void DisableInputCallback(const GattCharacteristic& inputChar,
                                     winrt::event_token token,
                                     const std::shared_ptr<InputCallbackGate>& callbackGate) {
        if (callbackGate) {
            std::lock_guard<std::mutex> callbackLock(callbackGate->mutex);
            callbackGate->active = false;
        }
        UnregisterInputCallback(inputChar, token);
    }

    void ClearPendingDualUnlocked() {
        pendingDualRight = ConnectedJoyCon{};
        pendingDualGyro = GyroSource::Right;
    }

    static void CleanupUnpublishedDual(DualJoyConPlayer& player) {
        player.running.store(false, std::memory_order_release);
        player.bufferCV.notify_one();
        if (player.updateThread.joinable()) player.updateThread.join();
        DisableInputCallback(player.leftJoyCon.inputChar,
                             player.leftInputChangedToken,
                             player.inputCallbackGate);
        UnregisterInputCallback(player.rightJoyCon.inputChar,
                                player.rightInputChangedToken);
        if (player.isXboxMode)
            vigem_target_x360_unregister_notification(player.ds4Controller);
        else
            vigem_target_ds4_unregister_notification(player.ds4Controller);
        StopVibrationWorker(*player.vibCtx);
        ViGEmManager::Instance().RemoveTarget(player.ds4Controller);
    }

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

                std::lock_guard<std::mutex> playersLock(singlePlayersMutex);

                // Ensure states vector matches player count
                if (states.size() < singlePlayers.size()) {
                    states.resize(singlePlayers.size());
                }

                auto now = std::chrono::steady_clock::now();

                for (size_t i = 0; i < singlePlayers.size(); ++i) {
                    auto& player = *singlePlayers[i];
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
                            input.mi.dwFlags = MOUSEEVENTF_MOVE;
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
                                input.mi.dwFlags = MOUSEEVENTF_MOVE;
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
    GyroSource pendingDualGyro = GyroSource::Right;
};

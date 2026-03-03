#pragma once
// DeviceManager - Async BLE scanning replacing blocking WaitForJoyCon
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <thread>

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;

constexpr uint16_t JOYCON_MANUFACTURER_ID = 1363;
inline const std::vector<uint8_t> JOYCON_MANUFACTURER_PREFIX = { 0x01, 0x00, 0x03, 0x7E };
inline const wchar_t* INPUT_REPORT_UUID_STR = L"ab7de9be-89fe-49ad-828f-118f09df7fd2";
inline const wchar_t* WRITE_COMMAND_UUID_STR = L"649d4ac9-8eb7-4e6c-af44-1ea54fe5f005";

struct ConnectedJoyCon {
    BluetoothLEDevice device = nullptr;
    GattCharacteristic inputChar = nullptr;
    GattCharacteristic writeChar = nullptr;
    uint64_t bleAddress = 0;
};

enum class ScanState { Idle, Scanning, Found, Error, Timeout };

class DeviceManager {
public:
    static DeviceManager& Instance() {
        static DeviceManager inst;
        return inst;
    }

    using ScanCallback = std::function<void(ConnectedJoyCon, ScanState)>;

    ScanState GetScanState() const { return state.load(); }

    void StartScan(ScanCallback callback) {
        if (state.load() == ScanState::Scanning) return;
        
        state.store(ScanState::Scanning);
        scanCallback = callback;

        // Run scanning in background thread so UI stays responsive
        // Wait for previous thread to finish if it's still joinable
        if (scanThread.joinable()) {
            // Previous scan should have been detached by StopScan or completed naturally
            // If still joinable, try to join with a brief wait via detach
            scanThread.detach();
        }
        scanThread = std::thread([this]() {
            RunScan();
        });
    }

    void StopScan() {
        cancelScan.store(true);
        state.store(ScanState::Idle);
        // Detach the scan thread instead of joining on the UI thread
        // to avoid blocking when WinRT async calls are in progress
        if (scanThread.joinable()) scanThread.detach();
    }

    ~DeviceManager() {
        StopScan();
    }

private:
    DeviceManager() = default;

    void RunScan() {
        cancelScan.store(false);
        ConnectedJoyCon cj{};
        BluetoothLEDevice device = nullptr;
        std::atomic<bool> connected{ false };

        BluetoothLEAdvertisementWatcher watcher;
        std::mutex mtx;
        std::condition_variable cv;

        watcher.Received([&](auto const&, auto const& args) {
            if (connected.load(std::memory_order_acquire)) return;
            if (cancelScan.load()) return;

            auto mfg = args.Advertisement().ManufacturerData();
            for (uint32_t i = 0; i < mfg.Size(); i++) {
                auto section = mfg.GetAt(i);
                if (section.CompanyId() != JOYCON_MANUFACTURER_ID) continue;
                auto reader = DataReader::FromBuffer(section.Data());
                std::vector<uint8_t> data(reader.UnconsumedBufferLength());
                reader.ReadBytes(data);
                if (data.size() >= JOYCON_MANUFACTURER_PREFIX.size() &&
                    std::equal(JOYCON_MANUFACTURER_PREFIX.begin(), JOYCON_MANUFACTURER_PREFIX.end(), data.begin())) {
                    
                    bool expected = false;
                    if (!connected.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                        return;

                    BluetoothLEDevice dev = BluetoothLEDevice::FromBluetoothAddressAsync(args.BluetoothAddress()).get();
                    if (!dev) {
                        connected.store(false, std::memory_order_release);
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        device = dev;
                    }
                    cv.notify_one();
                    return;
                }
            }
        });

        watcher.ScanningMode(BluetoothLEScanningMode::Active);
        watcher.Start();

        {
            std::unique_lock<std::mutex> lock(mtx);
            if (!cv.wait_for(lock, std::chrono::seconds(30), [&]() { 
                return connected.load(std::memory_order_acquire) || cancelScan.load(); 
            })) {
                watcher.Stop();
                state.store(ScanState::Timeout);
                if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Timeout);
                return;
            }
        }
        watcher.Stop();

        if (cancelScan.load()) {
            state.store(ScanState::Idle);
            return;
        }

        cj.device = device;
        cj.bleAddress = device.BluetoothAddress();

        // Check cancel before GATT discovery
        if (cancelScan.load()) {
            state.store(ScanState::Idle);
            return;
        }

        // Discover GATT services
        auto servicesResult = device.GetGattServicesAsync().get();
        if (cancelScan.load()) {
            state.store(ScanState::Idle);
            return;
        }
        if (servicesResult.Status() != GattCommunicationStatus::Success) {
            state.store(ScanState::Error);
            if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            return;
        }

        for (auto service : servicesResult.Services()) {
            if (cancelScan.load()) {
                state.store(ScanState::Idle);
                return;
            }
            auto charsResult = service.GetCharacteristicsAsync().get();
            if (cancelScan.load()) {
                state.store(ScanState::Idle);
                return;
            }
            if (charsResult.Status() != GattCommunicationStatus::Success) continue;
            for (auto characteristic : charsResult.Characteristics()) {
                if (characteristic.Uuid() == guid(INPUT_REPORT_UUID_STR))
                    cj.inputChar = characteristic;
                else if (characteristic.Uuid() == guid(WRITE_COMMAND_UUID_STR))
                    cj.writeChar = characteristic;
            }
        }

        // Request shortest connection interval (7.5ms) for minimal input lag
        // ThroughputOptimized has the lowest min interval among presets: 7.5ms–15ms
        try {
            auto connectionParams = BluetoothLEPreferredConnectionParameters::ThroughputOptimized();
            cj.device.RequestPreferredConnectionParameters(connectionParams);
        } catch (...) {}

        // Final cancel check before reporting success
        if (cancelScan.load()) {
            state.store(ScanState::Idle);
            return;
        }

        state.store(ScanState::Found);
        if (scanCallback) scanCallback(cj, ScanState::Found);
    }

    std::atomic<ScanState> state{ ScanState::Idle };
    std::atomic<bool> cancelScan{ false };
    ScanCallback scanCallback;
    std::thread scanThread;
};

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
#include "Logger.h"
#include "BluetoothLog.h"

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
        if (state.load() == ScanState::Scanning) {
            APP_LOG_WARNING("StartScan ignored: a BLE scan is already in progress");
            return;
        }
        
        APP_LOG_INFO("Starting BLE scan for Nintendo Switch 2 controller");
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
            try {
                RunScan();
            } catch (const winrt::hresult_error& e) {
                APP_LOG_ERROR("BLE scan thread failed: %s",
                              BluetoothLog::DescribeHResultError(e).c_str());
                state.store(ScanState::Error);
                if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            } catch (...) {
                APP_LOG_ERROR("BLE scan thread failed with an unknown exception");
                state.store(ScanState::Error);
                if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            }
        });
    }

    void StopScan() {
        APP_LOG_DEBUG("Stopping BLE scan");
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
        std::atomic<bool> watcherFailed{ false };

        BluetoothLEAdvertisementWatcher watcher;
        std::mutex mtx;
        std::condition_variable cv;

        watcher.Received([&](auto const&, auto const& args) {
            try {
                if (connected.load(std::memory_order_acquire)) return;
                if (cancelScan.load()) return;

                uint64_t address = args.BluetoothAddress();
                APP_LOG_DEBUG("BLE advertisement received (address: %llu, RSSI: %d dBm)",
                              address, args.RawSignalStrengthInDBm());

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

                        APP_LOG_INFO("Matching Joy-Con advertisement found (address: %llu, RSSI: %d dBm)",
                                     address, args.RawSignalStrengthInDBm());

                        BluetoothLEDevice dev = nullptr;
                        try {
                            dev = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
                        } catch (const winrt::hresult_error& e) {
                            APP_LOG_ERROR("Failed to create BluetoothLEDevice for address %llu: %s",
                                          address, BluetoothLog::DescribeHResultError(e).c_str());
                            connected.store(false, std::memory_order_release);
                            return;
                        } catch (...) {
                            APP_LOG_ERROR("Failed to create BluetoothLEDevice for address %llu: unknown exception",
                                          address);
                            connected.store(false, std::memory_order_release);
                            return;
                        }

                        if (!dev) {
                            APP_LOG_WARNING("BluetoothLEDevice::FromBluetoothAddressAsync returned null for address %llu",
                                            address);
                            connected.store(false, std::memory_order_release);
                            return;
                        }

                        APP_LOG_INFO("BluetoothLEDevice created (address: %llu, status: %s)",
                                     address, BluetoothLog::ConnectionStatusName(dev.ConnectionStatus()));

                        {
                            std::lock_guard<std::mutex> lock(mtx);
                            device = dev;
                        }
                        cv.notify_one();
                        return;
                    }
                }
            } catch (const winrt::hresult_error& e) {
                APP_LOG_ERROR("BLE advertisement callback failed: %s",
                              BluetoothLog::DescribeHResultError(e).c_str());
                connected.store(false, std::memory_order_release);
            } catch (...) {
                APP_LOG_ERROR("BLE advertisement callback failed with an unknown exception");
                connected.store(false, std::memory_order_release);
            }
        });

        watcher.Stopped([&](auto const&, auto const& args) {
            BluetoothError error = args.Error();
            if (error == BluetoothError::Success) {
                APP_LOG_DEBUG("BLE advertisement watcher stopped normally");
            } else {
                APP_LOG_WARNING("BLE advertisement watcher stopped: %s",
                                BluetoothLog::DescribeBluetoothError(error).c_str());
                watcherFailed.store(true, std::memory_order_release);
                cv.notify_one();
            }
        });

        watcher.ScanningMode(BluetoothLEScanningMode::Active);
        try {
            watcher.Start();
            APP_LOG_DEBUG("BLE advertisement watcher started (Active scanning)");
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("Failed to start BLE advertisement watcher: %s",
                          BluetoothLog::DescribeHResultError(e).c_str());
            state.store(ScanState::Error);
            if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            return;
        } catch (...) {
            APP_LOG_ERROR("Failed to start BLE advertisement watcher: unknown exception");
            state.store(ScanState::Error);
            if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            return;
        }

        {
            std::unique_lock<std::mutex> lock(mtx);
            if (!cv.wait_for(lock, std::chrono::seconds(30), [&]() { 
                return connected.load(std::memory_order_acquire) ||
                       cancelScan.load() ||
                       watcherFailed.load(std::memory_order_acquire);
            })) {
                watcher.Stop();
                APP_LOG_WARNING("BLE scan timed out after 30 seconds");
                state.store(ScanState::Timeout);
                if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Timeout);
                return;
            }
        }

        if (watcherFailed.load(std::memory_order_acquire)) {
            watcher.Stop();
            APP_LOG_ERROR("BLE scan aborted: advertisement watcher stopped with an error");
            state.store(ScanState::Error);
            if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            return;
        }

        watcher.Stop();

        if (cancelScan.load()) {
            APP_LOG_DEBUG("Scan cancelled before device handshake completed");
            state.store(ScanState::Idle);
            return;
        }

        cj.device = device;
        cj.bleAddress = device.BluetoothAddress();

        device.ConnectionStatusChanged([bleAddress = cj.bleAddress](BluetoothLEDevice const& sender, IInspectable const&) {
            APP_LOG_INFO("BLE device %llu connection status changed: %s",
                         bleAddress, BluetoothLog::ConnectionStatusName(sender.ConnectionStatus()));
        });

        // Check cancel before GATT discovery
        if (cancelScan.load()) {
            state.store(ScanState::Idle);
            return;
        }

        // Discover GATT services
        APP_LOG_DEBUG("Discovering GATT services for address %llu", cj.bleAddress);
        GattDeviceServicesResult servicesResult = nullptr;
        try {
            servicesResult = device.GetGattServicesAsync().get();
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("GetGattServicesAsync threw for address %llu: %s",
                          cj.bleAddress, BluetoothLog::DescribeHResultError(e).c_str());
            state.store(ScanState::Error);
            if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            return;
        } catch (...) {
            APP_LOG_ERROR("GetGattServicesAsync threw an unknown exception for address %llu", cj.bleAddress);
            state.store(ScanState::Error);
            if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            return;
        }

        if (cancelScan.load()) {
            state.store(ScanState::Idle);
            return;
        }
        if (servicesResult.Status() != GattCommunicationStatus::Success) {
            APP_LOG_ERROR("GATT service discovery failed for address %llu: %s",
                          cj.bleAddress,
                          BluetoothLog::DescribeGattStatus(servicesResult.Status(),
                                                           servicesResult.ProtocolError()).c_str());
            state.store(ScanState::Error);
            if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            return;
        }
        APP_LOG_DEBUG("GATT service discovery succeeded for address %llu (%u service(s))",
                      cj.bleAddress, servicesResult.Services().Size());

        for (auto service : servicesResult.Services()) {
            if (cancelScan.load()) {
                state.store(ScanState::Idle);
                return;
            }

            APP_LOG_DEBUG("Reading characteristics of GATT service %s",
                          BluetoothLog::GuidToString(service.Uuid()).c_str());

            GattCharacteristicsResult charsResult = nullptr;
            try {
                charsResult = service.GetCharacteristicsAsync().get();
            } catch (const winrt::hresult_error& e) {
                APP_LOG_WARNING("GetCharacteristicsAsync threw for service %s: %s",
                                BluetoothLog::GuidToString(service.Uuid()).c_str(),
                                BluetoothLog::DescribeHResultError(e).c_str());
                continue;
            } catch (...) {
                APP_LOG_WARNING("GetCharacteristicsAsync threw an unknown exception for service %s",
                                BluetoothLog::GuidToString(service.Uuid()).c_str());
                continue;
            }

            if (cancelScan.load()) {
                state.store(ScanState::Idle);
                return;
            }
            if (charsResult.Status() != GattCommunicationStatus::Success) {
                APP_LOG_WARNING("GetCharacteristicsAsync failed for service %s: %s",
                                BluetoothLog::GuidToString(service.Uuid()).c_str(),
                                BluetoothLog::DescribeGattStatus(charsResult.Status(),
                                                                 charsResult.ProtocolError()).c_str());
                continue;
            }

            for (auto characteristic : charsResult.Characteristics()) {
                auto uuid = characteristic.Uuid();
                APP_LOG_DEBUG("GATT characteristic discovered: %s",
                              BluetoothLog::GuidToString(uuid).c_str());
                if (uuid == guid(INPUT_REPORT_UUID_STR))
                    cj.inputChar = characteristic;
                else if (uuid == guid(WRITE_COMMAND_UUID_STR))
                    cj.writeChar = characteristic;
            }
        }

        if (!cj.inputChar) {
            APP_LOG_ERROR("Required input-report characteristic %S was not found for address %llu",
                          INPUT_REPORT_UUID_STR, cj.bleAddress);
            state.store(ScanState::Error);
            if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            return;
        }
        if (!cj.writeChar) {
            APP_LOG_WARNING("Write-command characteristic %S was not found for address %llu; "
                            "LED/vibration/command features will be unavailable",
                            WRITE_COMMAND_UUID_STR, cj.bleAddress);
        }

        // Request shortest connection interval (7.5ms) for minimal input lag
        // ThroughputOptimized has the lowest min interval among presets: 7.5ms–15ms
        try {
            auto connectionParams = BluetoothLEPreferredConnectionParameters::ThroughputOptimized();
            auto request = cj.device.RequestPreferredConnectionParameters(connectionParams);
            APP_LOG_DEBUG("Preferred connection-parameters request for address %llu: %s",
                          cj.bleAddress,
                          BluetoothLog::DescribeConnectionParametersRequestStatus(request.Status()).c_str());
        } catch (const winrt::hresult_error& e) {
            APP_LOG_DEBUG("RequestPreferredConnectionParameters failed for address %llu: %s",
                          cj.bleAddress, BluetoothLog::DescribeHResultError(e).c_str());
        } catch (...) {
            APP_LOG_DEBUG("RequestPreferredConnectionParameters failed for address %llu with an unknown exception",
                          cj.bleAddress);
        }

        // Final cancel check before reporting success
        if (cancelScan.load()) {
            state.store(ScanState::Idle);
            return;
        }

        state.store(ScanState::Found);
        APP_LOG_INFO("BLE scan succeeded (address: %llu, input characteristic: found, write characteristic: %s)",
                     cj.bleAddress,
                     cj.writeChar ? "found" : "missing");
        if (scanCallback) scanCallback(cj, ScanState::Found);
    }

    std::atomic<ScanState> state{ ScanState::Idle };
    std::atomic<bool> cancelScan{ false };
    ScanCallback scanCallback;
    std::thread scanThread;
};

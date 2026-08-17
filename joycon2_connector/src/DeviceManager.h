#pragma once
// DeviceManager - Async BLE scanning replacing blocking WaitForJoyCon
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <thread>
#include "Logger.h"
#include "BluetoothLog.h"
#include "ConnectionError.h"

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
inline const wchar_t* VIBRATION_JOYCON_LEFT_UUID_STR = L"289326cb-a471-485d-a8f4-240c14f18241";
inline const wchar_t* VIBRATION_JOYCON_RIGHT_UUID_STR = L"fa19b0fb-cd1f-46a7-84a1-bbb09e00c149";
inline const wchar_t* VIBRATION_PRO_UUID_STR = L"cc483f51-9258-427d-a939-630c31f72b05";
inline const wchar_t* VIBRATION_GC_UUID_STR = L"3f8fb670-ab25-45bf-b540-38c72834d064";

struct ConnectedJoyCon {
    BluetoothLEDevice device = nullptr;
    GattCharacteristic inputChar = nullptr;
    GattCharacteristic writeChar = nullptr;
    GattCharacteristic vibrationChar = nullptr;
    uint64_t bleAddress = 0;
};

enum class ScanState { Idle, Scanning, Found, Error, Timeout };

struct ScanResult {
    ConnectedJoyCon controller;
    ScanState state = ScanState::Idle;
    ConnectionError error;
};

inline const char* ScanStateName(ScanState state) {
    switch (state) {
    case ScanState::Idle:     return "Idle";
    case ScanState::Scanning: return "Scanning";
    case ScanState::Found:    return "Found";
    case ScanState::Error:    return "Error";
    case ScanState::Timeout:  return "Timeout";
    default:                  return "Unknown";
    }
}

class DeviceManager {
public:
    static DeviceManager& Instance() {
        static DeviceManager inst;
        return inst;
    }

    using ScanCallback = std::function<void(ScanResult)>;

    ScanState GetScanState() const { return state.load(); }

    bool StartScan(ScanCallback callback) {
        if (state.load() == ScanState::Scanning) {
            APP_LOG_WARNING("StartScan ignored: a BLE scan is already in progress");
            return false;
        }
        
        APP_LOG_INFO("Starting BLE scan for Nintendo Switch 2 controller "
                     "(timeout: 30 s, manufacturer ID: 0x%04X, required prefix: %s)",
                     JOYCON_MANUFACTURER_ID,
                     BluetoothLog::BytesToHex(JOYCON_MANUFACTURER_PREFIX.data(),
                                              JOYCON_MANUFACTURER_PREFIX.size()).c_str());
        state.store(ScanState::Scanning);
        // Run scanning in background thread so UI stays responsive
        // Wait for previous thread to finish if it's still joinable
        if (scanThread.joinable()) {
            // Previous scan should have been detached by StopScan or completed naturally
            // If still joinable, try to join with a brief wait via detach
            scanThread.detach();
        }
        scanThread = std::thread([this, callback = std::move(callback)]() {
            bool apartmentInitialized = false;
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                apartmentInitialized = true;
                RunScan(callback);
            } catch (const winrt::hresult_error& e) {
                APP_LOG_ERROR("BLE scan thread failed: %s",
                              BluetoothLog::DescribeHResultError(e).c_str());
                state.store(ScanState::Error);
                Notify(callback, { {}, ScanState::Error,
                    { ConnectionErrorKind::Internal, ConnectionErrorStage::ScanWatcher,
                      BluetoothLog::DescribeHResultError(e) } });
            } catch (...) {
                APP_LOG_ERROR("BLE scan thread failed with an unknown exception");
                state.store(ScanState::Error);
                Notify(callback, { {}, ScanState::Error,
                    { ConnectionErrorKind::Internal, ConnectionErrorStage::ScanWatcher, {} } });
            }
            if (apartmentInitialized) winrt::uninit_apartment();
        });
        return true;
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

    static void Notify(const ScanCallback& callback, ScanResult result) {
        if (!callback) return;
        try {
            callback(std::move(result));
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("BLE scan completion callback failed: %s",
                          BluetoothLog::DescribeHResultError(e).c_str());
        } catch (...) {
            APP_LOG_ERROR("BLE scan completion callback failed with an unknown exception");
        }
    }

    static ConnectionErrorKind ClassifyWatcherError(BluetoothError error) {
        switch (error) {
        case BluetoothError::RadioNotAvailable:
        case BluetoothError::DisabledByUser:
        case BluetoothError::ResourceInUse:
            return ConnectionErrorKind::BluetoothAdapterUnavailable;
        case BluetoothError::DisabledByPolicy:
        case BluetoothError::ConsentRequired:
            return ConnectionErrorKind::BluetoothAccessDenied;
        default:
            return ConnectionErrorKind::ScanFailed;
        }
    }

    static ConnectionErrorKind ClassifyScanStartHResult(int32_t code) {
        if (IsAccessDeniedHResult(code)) return ConnectionErrorKind::BluetoothAccessDenied;
        switch (static_cast<uint32_t>(code)) {
        case 0x80070015u: // ERROR_NOT_READY
        case 0x8007048Fu: // ERROR_DEVICE_NOT_CONNECTED
        case 0x80070490u: // ERROR_NOT_FOUND
        case 0x800710DFu: // ERROR_DEVICE_NOT_AVAILABLE
            return ConnectionErrorKind::BluetoothAdapterUnavailable;
        default:
            return ConnectionErrorKind::ScanFailed;
        }
    }

    void RunScan(const ScanCallback& callback) {
        cancelScan.store(false);
        ConnectedJoyCon cj{};
        BluetoothLEDevice device = nullptr;
        std::atomic<bool> candidateClaimed{ false };
        std::atomic<bool> deviceReady{ false };
        std::atomic<bool> watcherFailed{ false };
        std::atomic<BluetoothError> watcherError{ BluetoothError::Success };
        std::atomic<uint32_t> advertisementsReceived{ 0 };
        std::atomic<uint32_t> nintendoAdvertisementsReceived{ 0 };
        std::atomic<uint32_t> matchingAdvertisementsReceived{ 0 };
        auto scanStarted = std::chrono::steady_clock::now();

        BluetoothLEAdvertisementWatcher watcher;
        std::mutex mtx;
        std::condition_variable cv;
        std::mutex candidateErrorMutex;
        ConnectionError lastCandidateError;

        watcher.Received([&](auto const&, auto const& args) {
            try {
                if (candidateClaimed.load(std::memory_order_acquire)) return;
                if (cancelScan.load()) return;

                advertisementsReceived.fetch_add(1, std::memory_order_relaxed);
                uint64_t address = args.BluetoothAddress();
                auto advertisementType = args.AdvertisementType();
                APP_LOG_DEBUG("BLE advertisement received (address: %llu, RSSI: %d dBm, type: %s (%d), connectable: %s)",
                              address, args.RawSignalStrengthInDBm(),
                              BluetoothLog::AdvertisementTypeName(advertisementType),
                              static_cast<int>(advertisementType),
                              BluetoothLog::AdvertisementConnectabilityName(advertisementType));

                auto mfg = args.Advertisement().ManufacturerData();
                for (uint32_t i = 0; i < mfg.Size(); i++) {
                    auto section = mfg.GetAt(i);
                    if (section.CompanyId() != JOYCON_MANUFACTURER_ID) continue;

                    nintendoAdvertisementsReceived.fetch_add(1, std::memory_order_relaxed);

                    auto reader = DataReader::FromBuffer(section.Data());
                    std::vector<uint8_t> data(reader.UnconsumedBufferLength());
                    reader.ReadBytes(data);
                    std::string manufacturerData = BluetoothLog::BytesToHex(data.data(), data.size());
                    if (data.size() >= JOYCON_MANUFACTURER_PREFIX.size() &&
                        std::equal(JOYCON_MANUFACTURER_PREFIX.begin(), JOYCON_MANUFACTURER_PREFIX.end(), data.begin())) {
                        matchingAdvertisementsReceived.fetch_add(1, std::memory_order_relaxed);

                        bool expected = false;
                        if (!candidateClaimed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                            return;
                        cv.notify_one();

                        auto scanElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - scanStarted).count();
                        std::string localName = winrt::to_string(args.Advertisement().LocalName());
                        APP_LOG_INFO("Matching Joy-Con advertisement found (address: %llu, RSSI: %d dBm, "
                                     "type: %s (%d), connectable: %s, local_name: \"%s\", "
                                     "manufacturer_data: %s, scan_elapsed: %lld ms)",
                                     address, args.RawSignalStrengthInDBm(),
                                     BluetoothLog::AdvertisementTypeName(advertisementType),
                                     static_cast<int>(advertisementType),
                                     BluetoothLog::AdvertisementConnectabilityName(advertisementType),
                                     localName.c_str(), manufacturerData.c_str(), scanElapsedMs);

                        BluetoothLEDevice dev = nullptr;
                        auto createStarted = std::chrono::steady_clock::now();
                        try {
                            dev = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
                        } catch (const winrt::hresult_error& e) {
                            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - createStarted).count();
                            APP_LOG_ERROR("Failed to create BluetoothLEDevice for address %llu after %lld ms: %s",
                                          address, elapsedMs, BluetoothLog::DescribeHResultError(e).c_str());
                            {
                                std::lock_guard<std::mutex> lock(candidateErrorMutex);
                                lastCandidateError = {
                                    IsAccessDeniedHResult(e.code().value)
                                        ? ConnectionErrorKind::BluetoothAccessDenied
                                        : ConnectionErrorKind::DeviceUnavailable,
                                    ConnectionErrorStage::OpenDevice,
                                    BluetoothLog::DescribeHResultError(e)
                                };
                            }
                            candidateClaimed.store(false, std::memory_order_release);
                            cv.notify_one();
                            return;
                        } catch (...) {
                            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - createStarted).count();
                            APP_LOG_ERROR("Failed to create BluetoothLEDevice for address %llu after %lld ms: unknown exception",
                                          address, elapsedMs);
                            {
                                std::lock_guard<std::mutex> lock(candidateErrorMutex);
                                lastCandidateError = { ConnectionErrorKind::DeviceUnavailable,
                                    ConnectionErrorStage::OpenDevice, {} };
                            }
                            candidateClaimed.store(false, std::memory_order_release);
                            cv.notify_one();
                            return;
                        }

                        if (!dev) {
                            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - createStarted).count();
                            APP_LOG_WARNING("BluetoothLEDevice::FromBluetoothAddressAsync returned null for address %llu "
                                            "after %lld ms (device was not available in the Windows Bluetooth cache)",
                                            address, elapsedMs);
                            {
                                std::lock_guard<std::mutex> lock(candidateErrorMutex);
                                lastCandidateError = { ConnectionErrorKind::DeviceUnavailable,
                                    ConnectionErrorStage::OpenDevice, {} };
                            }
                            candidateClaimed.store(false, std::memory_order_release);
                            cv.notify_one();
                            return;
                        }

                        auto createElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - createStarted).count();
                        std::string deviceName = winrt::to_string(dev.Name());
                        const char* windowsPairingState = "unknown";
                        try {
                            windowsPairingState = dev.DeviceInformation().Pairing().IsPaired()
                                ? "paired" : "not paired";
                        } catch (const winrt::hresult_error& e) {
                            APP_LOG_DEBUG("Could not read Windows pairing state for address %llu: %s",
                                          address, BluetoothLog::DescribeHResultError(e).c_str());
                        } catch (...) {
                            APP_LOG_DEBUG("Could not read Windows pairing state for address %llu: unknown exception",
                                          address);
                        }

                        APP_LOG_INFO("BluetoothLEDevice created (address: %llu, name: \"%s\", address_type: %s, "
                                     "cached_connection_status: %s, Windows_pairing_state: %s, elapsed: %lld ms)",
                                     address, deviceName.c_str(),
                                     BluetoothLog::AddressTypeName(dev.BluetoothAddressType()),
                                     BluetoothLog::ConnectionStatusName(dev.ConnectionStatus()),
                                     windowsPairingState, createElapsedMs);

                        {
                            std::lock_guard<std::mutex> lock(mtx);
                            device = dev;
                        }
                        deviceReady.store(true, std::memory_order_release);
                        cv.notify_one();
                        return;
                    }

                    APP_LOG_DEBUG("Nintendo manufacturer advertisement did not match Joy-Con prefix "
                                  "(address: %llu, manufacturer_data: %s)",
                                  address, manufacturerData.c_str());
                }
            } catch (const winrt::hresult_error& e) {
                APP_LOG_ERROR("BLE advertisement callback failed: %s",
                              BluetoothLog::DescribeHResultError(e).c_str());
                candidateClaimed.store(false, std::memory_order_release);
                cv.notify_one();
            } catch (...) {
                APP_LOG_ERROR("BLE advertisement callback failed with an unknown exception");
                candidateClaimed.store(false, std::memory_order_release);
                cv.notify_one();
            }
        });

        watcher.Stopped([&](auto const&, auto const& args) {
            BluetoothError error = args.Error();
            if (error == BluetoothError::Success) {
                APP_LOG_DEBUG("BLE advertisement watcher stopped normally");
            } else {
                APP_LOG_WARNING("BLE advertisement watcher stopped: %s",
                                BluetoothLog::DescribeBluetoothError(error).c_str());
                watcherError.store(error, std::memory_order_release);
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
            Notify(callback, { {}, ScanState::Error,
                { ClassifyScanStartHResult(e.code().value),
                  ConnectionErrorStage::StartScan,
                  BluetoothLog::DescribeHResultError(e) } });
            return;
        } catch (...) {
            APP_LOG_ERROR("Failed to start BLE advertisement watcher: unknown exception");
            state.store(ScanState::Error);
            Notify(callback, { {}, ScanState::Error,
                { ConnectionErrorKind::ScanFailed, ConnectionErrorStage::StartScan, {} } });
            return;
        }

        bool scanTimedOut = false;
        {
            std::unique_lock<std::mutex> lock(mtx);
            auto scanDeadline = scanStarted + std::chrono::seconds(30);
            while (!deviceReady.load(std::memory_order_acquire)) {
                if (candidateClaimed.load(std::memory_order_acquire)) {
                    // FromBluetoothAddressAsync cannot be cancelled. Keep this context alive
                    // until the in-flight callback completes instead of timing out underneath it.
                    cv.wait(lock, [&]() {
                        return deviceReady.load(std::memory_order_acquire) ||
                               !candidateClaimed.load(std::memory_order_acquire);
                    });
                    continue;
                }

                if (cancelScan.load() || watcherFailed.load(std::memory_order_acquire)) {
                    break;
                }

                if (!cv.wait_until(lock, scanDeadline, [&]() {
                    return deviceReady.load(std::memory_order_acquire) ||
                           candidateClaimed.load(std::memory_order_acquire) ||
                           cancelScan.load() ||
                           watcherFailed.load(std::memory_order_acquire);
                })) {
                    scanTimedOut = true;
                    break;
                }
            }
        }

        if (scanTimedOut) {
            watcher.Stop();
            APP_LOG_WARNING("BLE scan timed out after 30 seconds (advertisements_received: %u, "
                            "Nintendo_manufacturer_sections: %u, matching_Joy-Con_advertisements: %u)",
                            advertisementsReceived.load(std::memory_order_relaxed),
                            nintendoAdvertisementsReceived.load(std::memory_order_relaxed),
                            matchingAdvertisementsReceived.load(std::memory_order_relaxed));
            APP_LOG_WARNING("No GATT connection was attempted. The controller may be powered off, out of range, "
                            "not advertising the expected controller payload, or the Bluetooth radio/driver may not be receiving advertisements");
            state.store(ScanState::Timeout);
            ConnectionError timeoutError = { ConnectionErrorKind::ScanTimeout,
                ConnectionErrorStage::ScanWatcher, {} };
            {
                std::lock_guard<std::mutex> lock(candidateErrorMutex);
                if (lastCandidateError) timeoutError = lastCandidateError;
            }
            Notify(callback, { {}, ScanState::Timeout, std::move(timeoutError) });
            return;
        }

        if (watcherFailed.load(std::memory_order_acquire)) {
            watcher.Stop();
            APP_LOG_ERROR("BLE scan aborted: advertisement watcher stopped with an error");
            state.store(ScanState::Error);
            BluetoothError error = watcherError.load(std::memory_order_acquire);
            Notify(callback, { {}, ScanState::Error,
                { ClassifyWatcherError(error), ConnectionErrorStage::ScanWatcher,
                  BluetoothLog::DescribeBluetoothError(error) } });
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

        auto handshakeStarted = std::chrono::steady_clock::now();
        device.ConnectionStatusChanged([bleAddress = cj.bleAddress, handshakeStarted](BluetoothLEDevice const& sender, IInspectable const&) {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - handshakeStarted).count();
            APP_LOG_INFO("BLE device %llu connection status changed after %lld ms: %s",
                         bleAddress, elapsedMs, BluetoothLog::ConnectionStatusName(sender.ConnectionStatus()));
        });

        // Check cancel before GATT discovery
        if (cancelScan.load()) {
            state.store(ScanState::Idle);
            return;
        }

        // Discover GATT services
        APP_LOG_DEBUG("Discovering GATT services for address %llu (first GATT operation; Windows may initiate or validate the BLE link)",
                      cj.bleAddress);
        GattDeviceServicesResult servicesResult = nullptr;
        auto discoveryStarted = std::chrono::steady_clock::now();
        try {
            servicesResult = device.GetGattServicesAsync().get();
        } catch (const winrt::hresult_error& e) {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - discoveryStarted).count();
            APP_LOG_ERROR("GetGattServicesAsync threw for address %llu after %lld ms "
                          "(connection_status: %s): %s",
                          cj.bleAddress, elapsedMs,
                          BluetoothLog::ConnectionStatusName(device.ConnectionStatus()),
                           BluetoothLog::DescribeHResultError(e).c_str());
            state.store(ScanState::Error);
            Notify(callback, { {}, ScanState::Error,
                { IsAccessDeniedHResult(e.code().value)
                    ? ConnectionErrorKind::GattAccessDenied
                    : ConnectionErrorKind::GattUnreachable,
                  ConnectionErrorStage::DiscoverServices,
                  BluetoothLog::DescribeHResultError(e) } });
            return;
        } catch (...) {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - discoveryStarted).count();
            APP_LOG_ERROR("GetGattServicesAsync threw an unknown exception for address %llu after %lld ms "
                          "(connection_status: %s)",
                          cj.bleAddress, elapsedMs,
                          BluetoothLog::ConnectionStatusName(device.ConnectionStatus()));
            state.store(ScanState::Error);
            Notify(callback, { {}, ScanState::Error,
                { ConnectionErrorKind::Internal, ConnectionErrorStage::DiscoverServices, {} } });
            return;
        }

        if (cancelScan.load()) {
            state.store(ScanState::Idle);
            return;
        }
        if (servicesResult.Status() != GattCommunicationStatus::Success) {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - discoveryStarted).count();
            APP_LOG_ERROR("GATT service discovery failed for address %llu after %lld ms "
                          "(connection_status: %s): %s",
                          cj.bleAddress, elapsedMs,
                          BluetoothLog::ConnectionStatusName(device.ConnectionStatus()),
                          BluetoothLog::DescribeGattStatus(servicesResult.Status(),
                                                            servicesResult.ProtocolError()).c_str());
            if (servicesResult.Status() == GattCommunicationStatus::Unreachable) {
                APP_LOG_WARNING("Connection diagnosis: a matching Joy-Con advertisement was received, but the GATT handshake "
                                "did not complete. WinRT does not expose whether the controller's sync button is held; "
                                "Unreachable is consistent with the controller not being in pairing mode, but can also mean "
                                "power/range loss, controller cooldown, or a transient Bluetooth link/driver failure");
            }
            state.store(ScanState::Error);
            ConnectionErrorKind kind = ConnectionErrorKind::GattProtocolError;
            if (servicesResult.Status() == GattCommunicationStatus::Unreachable)
                kind = ConnectionErrorKind::GattUnreachable;
            else if (servicesResult.Status() == GattCommunicationStatus::AccessDenied)
                kind = ConnectionErrorKind::GattAccessDenied;
            Notify(callback, { {}, ScanState::Error,
                { kind, ConnectionErrorStage::DiscoverServices,
                  BluetoothLog::DescribeGattStatus(servicesResult.Status(),
                                                   servicesResult.ProtocolError()) } });
            return;
        }
        auto discoveryElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - discoveryStarted).count();
        APP_LOG_DEBUG("GATT service discovery succeeded for address %llu in %lld ms (%u service(s))",
                      cj.bleAddress, discoveryElapsedMs, servicesResult.Services().Size());

        ConnectionError characteristicError;
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
                characteristicError = {
                    IsAccessDeniedHResult(e.code().value)
                        ? ConnectionErrorKind::GattAccessDenied
                        : ConnectionErrorKind::GattProtocolError,
                    ConnectionErrorStage::DiscoverCharacteristics,
                    BluetoothLog::DescribeHResultError(e)
                };
                continue;
            } catch (...) {
                APP_LOG_WARNING("GetCharacteristicsAsync threw an unknown exception for service %s",
                                BluetoothLog::GuidToString(service.Uuid()).c_str());
                characteristicError = { ConnectionErrorKind::Internal,
                    ConnectionErrorStage::DiscoverCharacteristics, {} };
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
                ConnectionErrorKind kind = ConnectionErrorKind::GattProtocolError;
                if (charsResult.Status() == GattCommunicationStatus::Unreachable)
                    kind = ConnectionErrorKind::GattUnreachable;
                else if (charsResult.Status() == GattCommunicationStatus::AccessDenied)
                    kind = ConnectionErrorKind::GattAccessDenied;
                characteristicError = { kind, ConnectionErrorStage::DiscoverCharacteristics,
                    BluetoothLog::DescribeGattStatus(charsResult.Status(), charsResult.ProtocolError()) };
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
                else if (uuid == guid(VIBRATION_JOYCON_LEFT_UUID_STR) ||
                         uuid == guid(VIBRATION_JOYCON_RIGHT_UUID_STR) ||
                         uuid == guid(VIBRATION_PRO_UUID_STR) ||
                         uuid == guid(VIBRATION_GC_UUID_STR))
                    cj.vibrationChar = characteristic;
            }
        }

        if (!cj.inputChar) {
            APP_LOG_ERROR("Required input-report characteristic %S was not found for address %llu",
                          INPUT_REPORT_UUID_STR, cj.bleAddress);
            state.store(ScanState::Error);
            if (!characteristicError) {
                characteristicError = { ConnectionErrorKind::UnsupportedController,
                    ConnectionErrorStage::DiscoverCharacteristics,
                    winrt::to_string(winrt::hstring(INPUT_REPORT_UUID_STR)) };
            }
            Notify(callback, { {}, ScanState::Error, std::move(characteristicError) });
            return;
        }
        if (!cj.writeChar) {
            APP_LOG_WARNING("Write-command characteristic %S was not found for address %llu; "
                            "LED and command features will be unavailable",
                            WRITE_COMMAND_UUID_STR, cj.bleAddress);
        }
        if (!cj.vibrationChar) {
            APP_LOG_WARNING("Vibration output characteristic was not found for address %llu; "
                            "runtime vibration will be unavailable",
                            cj.bleAddress);
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
        APP_LOG_INFO("BLE scan succeeded (address: %llu, input characteristic: found, write characteristic: %s, vibration characteristic: %s)",
                      cj.bleAddress,
                      cj.writeChar ? "found" : "missing",
                      cj.vibrationChar ? "found" : "missing");
        Notify(callback, { cj, ScanState::Found, {} });
    }

    std::atomic<ScanState> state{ ScanState::Idle };
    std::atomic<bool> cancelScan{ false };
    std::thread scanThread;
};

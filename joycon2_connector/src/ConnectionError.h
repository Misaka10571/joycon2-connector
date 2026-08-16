#pragma once

#include <cstdint>
#include <string>
#include <utility>

enum class ConnectionErrorKind {
    None,
    BluetoothAdapterUnavailable,
    BluetoothAccessDenied,
    ScanTimeout,
    ScanFailed,
    DeviceUnavailable,
    GattUnreachable,
    GattAccessDenied,
    GattProtocolError,
    UnsupportedController,
    NotificationSubscription,
    VirtualController,
    VirtualControllerUnknown,
    Internal
};

enum class ConnectionErrorStage {
    None,
    StartScan,
    ScanWatcher,
    OpenDevice,
    DiscoverServices,
    DiscoverCharacteristics,
    RegisterInput,
    SubscribeNotifications,
    CreateVirtualController,
    StorePlayer,
    StartWorker
};

struct ConnectionError {
    ConnectionErrorKind kind = ConnectionErrorKind::None;
    ConnectionErrorStage stage = ConnectionErrorStage::None;
    std::string detail;

    explicit operator bool() const { return kind != ConnectionErrorKind::None; }
};

inline bool IsAccessDeniedHResult(int32_t code) {
    return static_cast<uint32_t>(code) == 0x80070005u;
}

inline bool ReportConnectionError(ConnectionError* output,
                                  ConnectionErrorKind kind,
                                  ConnectionErrorStage stage,
                                  std::string detail = {}) {
    if (output) *output = { kind, stage, std::move(detail) };
    return false;
}

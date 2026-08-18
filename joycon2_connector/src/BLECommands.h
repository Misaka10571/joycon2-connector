#pragma once
// BLE Commands for Joy-Con / Pro Controller communication
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include "BluetoothLog.h"
#include <algorithm>
#include <vector>
#include <thread>
#include <chrono>

using namespace winrt;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;

inline void SendGenericCommand(GattCharacteristic const& characteristic, uint8_t cmdId, uint8_t subCmdId, const std::vector<uint8_t>& data) {
    if (!characteristic) return;

    DataWriter writer;
    writer.WriteByte(cmdId);
    writer.WriteByte(0x91);
    writer.WriteByte(0x01);
    writer.WriteByte(subCmdId);
    writer.WriteByte(0x00);
    writer.WriteByte(static_cast<uint8_t>(data.size()));
    writer.WriteByte(0x00);
    writer.WriteByte(0x00);
    for (uint8_t b : data) writer.WriteByte(b);

    IBuffer buffer = writer.DetachBuffer();
    APP_LOG_DEBUG("Sending BLE command cmd=0x%02X subCmd=0x%02X payload=%zu byte(s)",
                  cmdId, subCmdId, data.size());

    try {
        auto status = characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).get();
        if (status != GattCommunicationStatus::Success) {
            APP_LOG_WARNING("BLE command cmd=0x%02X subCmd=0x%02X failed: %s",
                            cmdId, subCmdId,
                            BluetoothLog::DescribeGattStatus(status).c_str());
        }
    } catch (const winrt::hresult_error& e) {
        APP_LOG_ERROR("BLE command cmd=0x%02X subCmd=0x%02X threw: %s",
                      cmdId, subCmdId, BluetoothLog::DescribeHResultError(e).c_str());
    } catch (...) {
        APP_LOG_ERROR("BLE command cmd=0x%02X subCmd=0x%02X threw an unknown exception",
                      cmdId, subCmdId);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(35));
}

enum class VibrationReportKind : uint8_t {
    SingleMotor,
    DualMotor,
    GameCube,
};

enum class GameCubeRumbleState : uint8_t {
    Off,
    On,
    Stop,
};

inline std::vector<uint8_t> BuildVibrationInitPayload(bool joyConProfile) {
    std::vector<uint8_t> data(0x14, 0x00);
    data[0] = 0x01;
    if (joyConProfile) {
        data[1] = 0x59;
        data[2] = 0x09;
        for (int i = 5; i <= 8; ++i) data[i] = 0xFF;
    } else {
        for (int i = 1; i <= 8; ++i) data[i] = 0xFF;
    }
    data[9] = 0x35;
    data[11] = 0x46;
    return data;
}

inline void SendCustomCommands(GattCharacteristic const& characteristic,
                               uint8_t featureFlags,
                               bool joyConProfile) {
    if (!characteristic) return;

    std::vector<uint8_t> featureData(4, 0x00);
    featureData[0] = featureFlags;

    SendGenericCommand(characteristic, 0x0C, 0x02, featureData);
    SendGenericCommand(characteristic, 0x0A, 0x08, BuildVibrationInitPayload(joyConProfile));
    SendGenericCommand(characteristic, 0x0C, 0x04, featureData);
}

inline void EmitSound(GattCharacteristic const& characteristic) {
    std::vector<uint8_t> data(4, 0x00);
    data[0] = 0x03;
    SendGenericCommand(characteristic, 0x0A, 0x02, data);
}

inline void SetPlayerLEDs(GattCharacteristic const& characteristic, uint8_t pattern) {
    std::vector<uint8_t> data(8, 0x00);
    data[0] = pattern;
    SendGenericCommand(characteristic, 0x09, 0x07, data);
}

inline void WriteZeroBytes(DataWriter& writer, int count) {
    for (int i = 0; i < count; ++i) writer.WriteByte(0x00);
}

inline uint16_t ScaleHDRumbleAmplitude(uint8_t motor) {
    // The protocol allows 0..1023, but existing drivers cap at about 450 to protect the LRA.
    constexpr uint16_t MAX_SAFE_AMPLITUDE = 450;
    return static_cast<uint16_t>((static_cast<uint32_t>(motor) * MAX_SAFE_AMPLITUDE + 127) / 255);
}

inline void WriteHDRumbleSlot(DataWriter& writer,
                              uint8_t largeMotor,
                              uint8_t smallMotor,
                              uint8_t sequenceCounter) {
    constexpr uint16_t HIGH_FREQUENCY = 0x187;
    constexpr uint16_t LOW_FREQUENCY = 0x112;

    uint16_t highAmplitude = ScaleHDRumbleAmplitude(smallMotor);
    uint16_t lowAmplitude = ScaleHDRumbleAmplitude(largeMotor);

    writer.WriteByte(0x50 | (sequenceCounter & 0x0F));
    for (int sample = 0; sample < 3; ++sample) {
        writer.WriteByte(static_cast<uint8_t>(HIGH_FREQUENCY));
        writer.WriteByte(static_cast<uint8_t>((HIGH_FREQUENCY >> 8) |
            ((highAmplitude & 0x3F) << 2)));
        writer.WriteByte(static_cast<uint8_t>((highAmplitude >> 6) |
            ((LOW_FREQUENCY & 0x0F) << 4)));
        writer.WriteByte(static_cast<uint8_t>((LOW_FREQUENCY >> 4) |
            ((lowAmplitude & 0x03) << 6)));
        writer.WriteByte(static_cast<uint8_t>(lowAmplitude >> 2));
    }
}

inline void SendVibrationReport(GattCharacteristic const& characteristic,
                                VibrationReportKind kind,
                                uint8_t largeMotor,
                                uint8_t smallMotor,
                                uint8_t sequenceCounter) {
    if (!characteristic) return;

    DataWriter writer;
    writer.WriteByte(0x00);

    switch (kind) {
    case VibrationReportKind::SingleMotor:
        WriteHDRumbleSlot(writer, largeMotor, smallMotor, sequenceCounter);
        break;
    case VibrationReportKind::DualMotor:
        WriteHDRumbleSlot(writer, largeMotor, smallMotor, sequenceCounter);
        WriteHDRumbleSlot(writer, largeMotor, smallMotor, sequenceCounter);
        break;
    case VibrationReportKind::GameCube:
        writer.WriteByte(static_cast<uint8_t>((largeMotor || smallMotor ? 0x60 : 0x50) |
            (sequenceCounter & 0x0F)));
        writer.WriteByte(0x00);
        writer.WriteByte(largeMotor || smallMotor ? 0x01 : 0x00);
        writer.WriteByte(0x00);
        WriteZeroBytes(writer, 37);
        break;
    }

    IBuffer buffer = writer.DetachBuffer();
    auto writeStarted = std::chrono::steady_clock::now();
    try {
        auto status = characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).get();
        if (status != GattCommunicationStatus::Success) {
            APP_LOG_DEBUG("Vibration report write failed: %s",
                          BluetoothLog::DescribeGattStatus(status).c_str());
        }
    } catch (const winrt::hresult_error& e) {
        APP_LOG_DEBUG("Vibration report write threw: %s",
                      BluetoothLog::DescribeHResultError(e).c_str());
    } catch (...) {
        APP_LOG_DEBUG("Vibration report write threw an unknown exception");
    }
    auto writeElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - writeStarted).count();
    if (writeElapsedMs >= 50) {
        APP_LOG_DEBUG("Vibration report write took %lld ms", writeElapsedMs);
    }
}

inline void SendGameCubeVibrationReport(GattCharacteristic const& characteristic,
                                         GameCubeRumbleState state,
                                         uint8_t sequenceCounter) {
    if (!characteristic) return;

    DataWriter writer;
    writer.WriteByte(0x00);
    writer.WriteByte(static_cast<uint8_t>((state == GameCubeRumbleState::On ? 0x60 : 0x50) |
        (sequenceCounter & 0x0F)));
    writer.WriteByte(state == GameCubeRumbleState::Stop ? 0x02 : 0x00);
    writer.WriteByte(state == GameCubeRumbleState::On ? 0x01 : 0x00);
    writer.WriteByte(0x00);
    WriteZeroBytes(writer, 37);

    IBuffer buffer = writer.DetachBuffer();
    try {
        auto status = characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).get();
        if (status != GattCommunicationStatus::Success) {
            APP_LOG_DEBUG("GameCube vibration report write failed: %s",
                          BluetoothLog::DescribeGattStatus(status).c_str());
        }
    } catch (const winrt::hresult_error& e) {
        APP_LOG_DEBUG("GameCube vibration report write threw: %s",
                      BluetoothLog::DescribeHResultError(e).c_str());
    } catch (...) {
        APP_LOG_DEBUG("GameCube vibration report write threw an unknown exception");
    }
}

// Non-blocking versions for use inside BLE notification callbacks
// Avoids blocking the callback thread which would freeze input processing
inline void SetPlayerLEDsAsync(GattCharacteristic characteristic, uint8_t pattern) {
    std::thread([characteristic, pattern]() {
        SetPlayerLEDs(characteristic, pattern);
    }).detach();
}

inline void EmitSoundAsync(GattCharacteristic characteristic) {
    std::thread([characteristic]() {
        EmitSound(characteristic);
    }).detach();
}


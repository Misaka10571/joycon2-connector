#pragma once
// BLE Commands for Joy-Con / Pro Controller communication
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include "BluetoothLog.h"
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

inline void SendCustomCommands(GattCharacteristic const& characteristic) {
    std::vector<std::vector<uint8_t>> commands = {
        { 0x0c, 0x91, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 },
        { 0x0c, 0x91, 0x01, 0x04, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 }
    };

    for (size_t index = 0; index < commands.size(); ++index) {
        const auto& cmd = commands[index];
        auto writer = DataWriter();
        writer.WriteBytes(cmd);
        IBuffer buffer = writer.DetachBuffer();
        APP_LOG_DEBUG("Sending custom init command %zu/%zu (%zu byte(s))",
                      index + 1, commands.size(), cmd.size());

        try {
            auto status = characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).get();
            if (status != GattCommunicationStatus::Success) {
                APP_LOG_WARNING("Custom init command %zu failed: %s",
                                index + 1, BluetoothLog::DescribeGattStatus(status).c_str());
            }
        } catch (const winrt::hresult_error& e) {
            APP_LOG_ERROR("Custom init command %zu threw: %s",
                          index + 1, BluetoothLog::DescribeHResultError(e).c_str());
        } catch (...) {
            APP_LOG_ERROR("Custom init command %zu threw an unknown exception", index + 1);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

inline void EmitSound(GattCharacteristic const& characteristic) {
    std::vector<uint8_t> data(8, 0x00);
    data[0] = 0x04;
    SendGenericCommand(characteristic, 0x0A, 0x02, data);
}

inline void SetPlayerLEDs(GattCharacteristic const& characteristic, uint8_t pattern) {
    std::vector<uint8_t> data(8, 0x00);
    data[0] = pattern;
    SendGenericCommand(characteristic, 0x09, 0x07, data);
}

// Vibration sample IDs (from protocol reverse engineering)
enum VibrationSample : uint8_t {
    VIB_NONE        = 0x00,  // No sound / stop
    VIB_BUZZ        = 0x01,  // 1s sustained buzz
    VIB_FIND        = 0x02,  // Find controller (high pitch + beeps)
    VIB_CONNECT     = 0x03,  // Button click sound
    VIB_PAIRING     = 0x04,  // Pairing sound
    VIB_STRONG_THUNK= 0x05,  // Strong thunk impact
    VIB_DUN         = 0x06,  // Short dun
    VIB_DING        = 0x07,  // Short ding
};

// Send a predefined vibration sample via the command channel
inline void SendVibrationSample(GattCharacteristic const& characteristic, uint8_t sampleId) {
    std::vector<uint8_t> data(8, 0x00);
    data[0] = sampleId;
    SendGenericCommand(characteristic, 0x0A, 0x02, data);
}

// Send raw vibration data (16-byte frame per motor, protocol format 0x5N)
// sequenceCounter should increment per frame (only lower 4 bits used)
inline void SendRawVibration(GattCharacteristic const& characteristic,
                             bool enabled, const uint8_t vibData[12],
                             uint8_t sequenceCounter) {
    if (!characteristic) return;

    DataWriter writer;
    // Packet A
    writer.WriteByte(0x00);                                      // [0] frame header
    writer.WriteByte(0x50 | (sequenceCounter & 0x0F));           // [1] vibration marker + seq
    writer.WriteByte(enabled ? 0x01 : 0x00);                     // [2] enabled flag
    for (int i = 0; i < 12; ++i) writer.WriteByte(vibData[i]);   // [3..14] vibration payload
    writer.WriteByte(0x00);                                      // [15] padding

    IBuffer buffer = writer.DetachBuffer();
    try {
        auto status = characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).get();
        if (status != GattCommunicationStatus::Success) {
            APP_LOG_DEBUG("Raw vibration write failed: %s",
                          BluetoothLog::DescribeGattStatus(status).c_str());
        }
    } catch (const winrt::hresult_error& e) {
        APP_LOG_DEBUG("Raw vibration write threw: %s",
                      BluetoothLog::DescribeHResultError(e).c_str());
    } catch (...) {
        APP_LOG_DEBUG("Raw vibration write threw an unknown exception");
    }
    // No sleep — raw vibration needs low latency
}

// Encode DS4 motor values into a 12-byte Switch 2 raw vibration payload.
// The exact 12-byte format for Switch 2 HD Rumble is not yet fully reverse-engineered.
// The critical fix is using the raw vibration channel (0x5N packet) instead of
// predefined sound/haptic samples (cmd 0x0A), which cause audible beeping on Pro2.
inline void EncodeVibrationPayload(uint8_t largeMotor, uint8_t smallMotor, uint8_t outData[12]) {
    for (int i = 0; i < 12; ++i) outData[i] = 0;
    // LargeMotor -> low-frequency rumble, SmallMotor -> high-frequency rumble.
    // Place amplitude values at payload positions. This encoding may need
    // refinement once the full Switch 2 vibration protocol is documented.
    outData[0] = largeMotor;
    outData[1] = smallMotor;
    outData[2] = largeMotor;
    outData[3] = smallMotor;
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

// Async vibration sample for use from ViGEm callbacks
inline void SendVibrationSampleAsync(GattCharacteristic characteristic, uint8_t sampleId) {
    std::thread([characteristic, sampleId]() {
        SendVibrationSample(characteristic, sampleId);
    }).detach();
}

// Async raw vibration for use from ViGEm callbacks (avoids blocking callback thread)
inline void SendRawVibrationAsync(GattCharacteristic characteristic,
                                   bool enabled, const uint8_t vibData[12],
                                   uint8_t sequenceCounter) {
    std::vector<uint8_t> data(vibData, vibData + 12);
    std::thread([characteristic, enabled, data, sequenceCounter]() {
        SendRawVibration(characteristic, enabled, data.data(), sequenceCounter);
    }).detach();
}

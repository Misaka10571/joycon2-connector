#pragma once
// BLE Commands for Joy-Con / Pro Controller communication
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
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
    characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).get();
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
}

inline void SendCustomCommands(GattCharacteristic const& characteristic) {
    std::vector<std::vector<uint8_t>> commands = {
        { 0x0c, 0x91, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 },
        { 0x0c, 0x91, 0x01, 0x04, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 }
    };

    for (const auto& cmd : commands) {
        auto writer = DataWriter();
        writer.WriteBytes(cmd);
        IBuffer buffer = writer.DetachBuffer();
        characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).get();
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

// Send raw vibration data as a 32-byte frame (two 16-byte sub-packets, protocol format 0x5N)
// Each sub-packet: [0x00][0x5N|seq][enabled][12-byte motor payload][0x00]
// Packet A = large motor (low-frequency), Packet B = small motor (high-frequency)
// sequenceCounter should increment per frame (only lower 4 bits used)
inline void SendRawVibration(GattCharacteristic const& characteristic,
                             bool enabledA, const uint8_t vibDataA[12],
                             bool enabledB, const uint8_t vibDataB[12],
                             uint8_t sequenceCounter) {
    if (!characteristic) return;

    DataWriter writer;
    // Packet A (bytes 0-15): large motor / left motor
    writer.WriteByte(0x00);                                      // [0]  frame header
    writer.WriteByte(0x50 | (sequenceCounter & 0x0F));           // [1]  vibration marker + seq
    writer.WriteByte(enabledA ? 0x01 : 0x00);                    // [2]  enabled flag
    for (int i = 0; i < 12; ++i) writer.WriteByte(vibDataA[i]);  // [3..14] vibration payload A
    writer.WriteByte(0x00);                                      // [15] padding
    // Packet B (bytes 16-31): small motor / right motor
    writer.WriteByte(0x00);                                      // [16] frame header
    writer.WriteByte(0x50 | (sequenceCounter & 0x0F));           // [17] vibration marker + seq
    writer.WriteByte(enabledB ? 0x01 : 0x00);                    // [18] enabled flag
    for (int i = 0; i < 12; ++i) writer.WriteByte(vibDataB[i]);  // [19..30] vibration payload B
    writer.WriteByte(0x00);                                      // [31] padding

    IBuffer buffer = writer.DetachBuffer();
    characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).get();
    // No sleep — raw vibration needs low latency
}

// Encode a single motor amplitude into a 12-byte Switch 2 raw vibration payload.
// The exact 12-byte format for Switch 2 HD Rumble is not yet fully reverse-engineered.
// The critical fix is using the raw vibration channel (0x5N packet) instead of
// predefined sound/haptic samples (cmd 0x0A), which cause audible beeping on Pro2.
inline void EncodeVibrationPayload(uint8_t amplitude, uint8_t outData[12]) {
    for (int i = 0; i < 12; ++i) outData[i] = 0;
    // Place amplitude at multiple positions for redundancy/compatibility.
    // This encoding may need refinement once the full Switch 2 vibration
    // protocol is documented.
    outData[0] = amplitude;
    outData[1] = amplitude;
    outData[2] = amplitude;
    outData[3] = amplitude;
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
                                   bool enabledA, const uint8_t vibDataA[12],
                                   bool enabledB, const uint8_t vibDataB[12],
                                   uint8_t sequenceCounter) {
    std::vector<uint8_t> dataA(vibDataA, vibDataA + 12);
    std::vector<uint8_t> dataB(vibDataB, vibDataB + 12);
    std::thread([characteristic, enabledA, dataA, enabledB, dataB, sequenceCounter]() {
        SendRawVibration(characteristic, enabledA, dataA.data(), enabledB, dataB.data(), sequenceCounter);
    }).detach();
}

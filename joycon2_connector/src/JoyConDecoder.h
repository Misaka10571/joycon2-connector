#pragma once
#include <vector>
#include <utility>
#include <cstdint>
#include <Windows.h>
#include <ViGEm/Client.h>

enum class JoyConSide { Left, Right };
enum class JoyConOrientation { Upright, Sideways };
enum class GyroSource { Both, Left, Right };

struct StickData {
    int16_t x;
    int16_t y;
    BYTE rx;
    BYTE ry;
};

struct MotionData {
    SHORT gyroX, gyroY, gyroZ;
    SHORT accelX, accelY, accelZ;
};

// Pass side and orientation explicitly now:
DS4_REPORT_EX GenerateDS4Report(const std::vector<uint8_t>& buffer, JoyConSide side, JoyConOrientation orientation);
DS4_REPORT_EX GenerateDualJoyConDS4Report(const std::vector<uint8_t>& leftBuffer, const std::vector<uint8_t>& rightBuffer, GyroSource gyroSource);
DS4_REPORT_EX GenerateProControllerReport(const std::vector<uint8_t>& buffer);
DS4_REPORT_EX GenerateNSOGCReport(const std::vector<uint8_t>& buffer);

uint32_t ExtractButtonState(const std::vector<uint8_t>& buffer);
std::pair<int16_t, int16_t> GetRawOpticalMouse(const std::vector<uint8_t>& buffer);
StickData DecodeJoystick(const std::vector<uint8_t>& buffer, JoyConSide side, JoyConOrientation orientation);
MotionData DecodeMotion(const std::vector<uint8_t>& buffer);

// Post-process a DS4 report to swap A⇄B (CROSS⇄CIRCLE) and X⇄Y (SQUARE⇄TRIANGLE)
inline void ApplyABXYSwap(DS4_REPORT_EX& report) {
    USHORT oldButtons = report.Report.wButtons;
    // Read current state of the four face buttons
    bool hasCross    = (oldButtons & DS4_BUTTON_CROSS) != 0;
    bool hasCircle   = (oldButtons & DS4_BUTTON_CIRCLE) != 0;
    bool hasSquare   = (oldButtons & DS4_BUTTON_SQUARE) != 0;
    bool hasTriangle = (oldButtons & DS4_BUTTON_TRIANGLE) != 0;
    // Clear all four
    report.Report.wButtons &= ~(DS4_BUTTON_CROSS | DS4_BUTTON_CIRCLE | DS4_BUTTON_SQUARE | DS4_BUTTON_TRIANGLE);
    // Re-set swapped: CROSS⇄CIRCLE, SQUARE⇄TRIANGLE
    if (hasCross)    report.Report.wButtons |= DS4_BUTTON_CIRCLE;
    if (hasCircle)   report.Report.wButtons |= DS4_BUTTON_CROSS;
    if (hasSquare)   report.Report.wButtons |= DS4_BUTTON_TRIANGLE;
    if (hasTriangle) report.Report.wButtons |= DS4_BUTTON_SQUARE;
}

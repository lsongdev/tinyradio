#pragma once
#include <stdint.h>

// Sunnygold SI4732 PCB 1.2; GPIO numbers, not module pad numbers.
namespace hal { namespace board {
constexpr int battery = 4;
constexpr float batteryDivider = 2.0f; // R8 = R10 = 100k, measured node VCC_BAT
constexpr float batteryCalibration = 1.0f;
constexpr int lcdReset = 5, lcdCs = 6, lcdDc = 7, lcdWr = 8, lcdRd = 9;
constexpr int lcdData[] = {39, 40, 41, 42, 45, 46, 47, 48};
constexpr int backlight = 38;
constexpr int radioEnable = 15, radioReset = 16, i2cScl = 17, i2cSda = 18;
constexpr int audioMute = 3, amplifierEnable = 10;
constexpr int encoderA = 2, encoderB = 1, encoderButton = 21;
constexpr int bootButton = 0;
} }

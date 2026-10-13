#include "battery.h"
#include "board.h"
#include <Arduino.h>
namespace hal {
void Battery::begin() {
  pinMode(board::battery, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(board::battery, ADC_11db);
}
BatteryReading Battery::read() const {
  uint32_t sum = 0;
  for (int i = 0; i < 32; ++i) sum += analogReadMilliVolts(board::battery);
  const uint32_t mv = (sum + 16) / 32;
  const float volts = mv * board::batteryDivider * board::batteryCalibration / 1000.0f;
  // A deliberately approximate voltage estimate, not a fuel gauge.
  const int percent = constrain(static_cast<int>((volts - 3.2f) * 100.0f), 0, 100);
  return {mv, volts, static_cast<uint8_t>(percent), volts > 4.35f,
          volts >= 2.5f && volts <= 5.5f};
}
}

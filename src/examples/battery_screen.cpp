#include "battery_screen.h"
#include "hal/audio.h"
#include "hal/battery.h"
#include "hal/board.h"
#include "hal/button.h"
#include "hal/display.h"
#include "hal/encoder.h"
#include "hal/radio.h"
#include <Arduino.h>
namespace {
hal::Display display;
hal::Battery battery;
hal::Encoder encoder;
hal::Button button(hal::board::encoderButton);
hal::Audio audio;
hal::RadioHardware radio;
hal::BatteryReading reading{};
bool displayReady = false, details = false, dirty = true;
int brightness = 128, turns = 0, presses = 0;
uint32_t lastSample = 0;
void draw() {
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.setCursor(10, 8); display.print("ATS MINI / BATTERY");
  display.setCursor(10, 39);
  if (!reading.plausible) display.print("Check battery ADC");
  else if (reading.externalPowerLikely) display.printf("Supply %.2f V (USB?)", reading.volts);
  else display.printf("%.2f V   ~%u%%", reading.volts, reading.estimatedPercent);
  if (details) {
    display.setCursor(10, 75); display.printf("ADC: %lu mV", static_cast<unsigned long>(reading.adcMillivolts));
    display.setCursor(10, 101); display.printf("Turns:%d Press:%d", turns, presses);
  } else {
    display.drawRect(10, 77, 280, 28, TFT_WHITE);
    if (reading.plausible && !reading.externalPowerLikely)
      display.fillRect(13, 80, 274 * reading.estimatedPercent / 100, 22,
                       reading.estimatedPercent < 20 ? TFT_RED : TFT_GREEN);
  }
  display.setTextSize(1);
  display.setCursor(10, 125); display.printf("Backlight: %d / 255 | Audio muted", brightness);
  display.setCursor(10, 145); display.print("Turn: brightness  Press: details");
  display.fillRect(10, 158, 14, 8, TFT_RED);
  display.fillRect(30, 158, 14, 8, TFT_GREEN);
  display.fillRect(50, 158, 14, 8, TFT_BLUE);
  display.setCursor(72, 158); display.print("RGB check");
}
}
namespace examples {
void beginBatteryScreen() {
  Serial.begin(115200);
  audio.begin();
  radio.begin(); // Keep radio LDO off during battery test.
  battery.begin(); encoder.begin(); button.begin();
  displayReady = display.begin();
  Serial.printf("Battery demo started. LCD init: %s\n", displayReady ? "OK" : "FAILED");
  reading = battery.read();
  lastSample = millis();
}
void updateBatteryScreen() {
  const uint32_t now = millis();
  const int delta = encoder.update();
  if (delta) {
    turns += delta;
    brightness = constrain(brightness + delta * 8, 16, 255);
    if (displayReady) display.setBrightness(brightness);
    dirty = true;
  }
  if (button.update(now)) { ++presses; details = !details; dirty = true; }
  if (now - lastSample >= 1000) {
    lastSample = now; reading = battery.read(); dirty = true;
    Serial.printf("ADC=%lu mV supply=%.3f V approx=%u%% USB?=%d valid=%d turns=%d presses=%d\n",
      static_cast<unsigned long>(reading.adcMillivolts), reading.volts, reading.estimatedPercent,
      reading.externalPowerLikely, reading.plausible, turns, presses);
  }
  if (dirty) { dirty = false; if (displayReady) draw(); }
}
}

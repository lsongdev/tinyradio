#include "board.h"
#include "pins.h"
#include "hal/button.h"
#include "hal/encoder.h"
#include "hal/i2c.h"
#include <Arduino.h>

static hal_encoder_t encoder;
static hal_button_t button;
// Bound each bus transaction independently of the SI4732 command-level CTS deadline.
static hal_i2c_t radio_i2c = {0, 20};
static bool radio_powered = false;

void board_begin(void) {
  digitalWrite(PIN_AUDIO_MUTE, HIGH);
  digitalWrite(PIN_AMPLIFIER_ENABLE, LOW);
  pinMode(PIN_AUDIO_MUTE, OUTPUT);
  pinMode(PIN_AMPLIFIER_ENABLE, OUTPUT);
  pinMode(PIN_BATTERY, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATTERY, ADC_11db);
  hal_encoder_begin(&encoder, PIN_ENCODER_A, PIN_ENCODER_B, true);
  hal_button_begin(&button, PIN_ENCODER_BUTTON, true);
}

void board_audio_set_muted(bool muted) { digitalWrite(PIN_AUDIO_MUTE, muted ? HIGH : LOW); }
void board_speaker_set_enabled(bool enabled) { digitalWrite(PIN_AMPLIFIER_ENABLE, enabled ? HIGH : LOW); }

hal_battery_reading_t board_battery_read(void) {
  uint32_t sum = 0;
  for (int i = 0; i < 32; ++i) sum += analogReadMilliVolts(PIN_BATTERY);
  const uint32_t mv = (sum + 16) / 32;
  const float volts = mv * BATTERY_DIVIDER * BATTERY_CALIBRATION / 1000.0f;
  int percent = (int)((volts - 3.2f) * 100.0f);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  const hal_battery_reading_t reading = {mv, volts, (uint8_t)percent, volts > 4.35f, volts >= 2.5f && volts <= 5.5f};
  return reading;
}

int board_encoder_read(void) { return hal_encoder_read(&encoder); }
bool board_button_update(uint32_t now) { return hal_button_update(&button, now); }
bool board_button_pressed(void) { return button.pressed; }
uint8_t board_keys_read(uint32_t now, uint8_t* repeats) { (void)now; *repeats = 0; return 0; }
bool board_rtc_read(int64_t* utc_seconds) { (void)utc_seconds; return false; }
bool board_rtc_write(int64_t utc_seconds) { (void)utc_seconds; return false; }
bool board_environment_poll(uint32_t now, hal_environment_reading_t* reading) { (void)now; (void)reading; return false; }

bool board_radio_begin(void) {
  digitalWrite(PIN_RADIO_ENABLE, LOW);
  digitalWrite(PIN_RADIO_RESET, LOW);
  pinMode(PIN_RADIO_ENABLE, OUTPUT);
  pinMode(PIN_RADIO_RESET, OUTPUT);
  // I2C pull-ups belong to VCC_RADIO. Leave the bus floating while off.
  pinMode(PIN_I2C_SDA, INPUT);
  pinMode(PIN_I2C_SCL, INPUT);
  return true;
}
void board_radio_set_powered(bool powered) {
  if (powered == radio_powered) return;
  if (!powered) {
    hal_i2c_end(&radio_i2c);
    pinMode(PIN_I2C_SDA, INPUT);
    pinMode(PIN_I2C_SCL, INPUT);
    digitalWrite(PIN_RADIO_RESET, LOW);
  }
  digitalWrite(PIN_RADIO_ENABLE, powered ? HIGH : LOW);
  radio_powered = powered;
  if (powered) {
    delay(10);
    hal_i2c_begin(&radio_i2c, PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  }
}
void board_radio_reset(void) {
  if (!radio_powered) return;
  digitalWrite(PIN_RADIO_RESET, LOW); delay(10);
  digitalWrite(PIN_RADIO_RESET, HIGH); delay(10);
}
i2c_bus_t board_radio_bus(void) { return hal_i2c_bus(&radio_i2c); }

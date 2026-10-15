#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "boards/keys.h"
#include "hal/battery.h"
#include "hal/environment.h"
#include "drivers/i2c_bus.h"
#define BOARD_NAME "ATS Mini"
#define BOARD_STORAGE_NAMESPACE "ats-mini" // Keep: existing units store settings here.
#define BOARD_SCREEN_WIDTH 320
#define BOARD_SCREEN_HEIGHT 170
#define BOARD_HAS_RTC 0 // No calendar chip: time is lost at power off.
#define BOARD_HAS_AIR 0 // No frequency converter.
#define BOARD_RADIO_EXTERNAL_CLOCK 0 // SI4732 has its own 32.768 kHz crystal.
#define BOARD_CAN_POWER_OFF 0        // Hardware power switch; no board_power_off().

// Safe outputs (audio muted, amplifier off), battery ADC, encoder and button.
// The display and radio rail are started separately by the application.
void board_begin(void);

// Panel and backlight. Drawing goes through the UI, which gets the LovyanGFX
// device from boards/display.hpp.
bool board_display_begin(void);
void board_display_set_brightness(uint8_t value);

// Analog SI4732 audio: GPIO controls mute and amp; no MCU DAC/I2S speaker.
void board_audio_set_muted(bool muted);
void board_speaker_set_enabled(bool enabled);

// Averaged supply estimate; a deliberately approximate voltage, not a fuel gauge.
hal_battery_reading_t board_battery_read(void);

int board_encoder_read(void);
bool board_button_update(uint32_t now); // True once per debounced press.
bool board_button_pressed(void);
uint8_t board_keys_read(uint32_t now, uint8_t* repeats); // No extra keys: always 0.

// No RTC or environment sensor: these always return false.
bool board_rtc_read(int64_t* utc_seconds);
bool board_rtc_write(int64_t utc_seconds);
bool board_environment_poll(uint32_t now, hal_environment_reading_t* reading);

// VCC_RADIO switch, SI4732 reset line, and the I2C bus powered by that rail.
bool board_radio_begin(void); // False when the radio's support hardware failed.
void board_radio_set_powered(bool powered);
void board_radio_reset(void);
i2c_bus_t board_radio_bus(void);
// Direct antenna path: the SI4732 tunes every RF frequency itself.
bool board_frontend_set(uint32_t rf_hz);
uint32_t board_frontend_receiver_hz(uint32_t rf_hz);

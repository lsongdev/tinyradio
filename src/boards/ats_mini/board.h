#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "hal/battery.h"
#include "drivers/i2c_bus.h"
#define BOARD_NAME "ATS Mini"
#define BOARD_STORAGE_NAMESPACE "ats-mini" // Keep: existing units store settings here.
#define BOARD_SCREEN_WIDTH 320
#define BOARD_SCREEN_HEIGHT 170
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

// VCC_RADIO switch, SI4732 reset line, and the I2C bus powered by that rail.
bool board_radio_begin(void); // False when the radio's support hardware failed.
void board_radio_set_powered(bool powered);
void board_radio_reset(void);
i2c_bus_t board_radio_bus(void);

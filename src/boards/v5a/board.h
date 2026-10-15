#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "drivers/i2c_bus.h"
#include "hal/battery.h"
#define BOARD_NAME "GeekNest V5A"
#define BOARD_STORAGE_NAMESPACE "geeknest-v5a" // Distinct from the original firmware's "radio".
#define BOARD_SCREEN_WIDTH 320
#define BOARD_SCREEN_HEIGHT 240
#define BOARD_RADIO_EXTERNAL_CLOCK 1 // SI4732 RCLK comes from Si5351 CLK1.
#define BOARD_CAN_POWER_OFF 1        // Software power latch; see board_power_off().

// Takes the power latch, then puts audio in a safe state and starts inputs,
// battery ADC and the shared I2C bus. Call before anything else.
void board_begin(void);

// Panel and backlight. Drawing goes through the UI, which gets the LovyanGFX
// device from boards/display.hpp.
bool board_display_begin(void);
void board_display_set_brightness(uint8_t value);
// Backlight off (held through deep sleep) and panel asleep; used by board_power_off().
void board_display_sleep(void);

// No dedicated mute line: the SI4732 mutes itself. The amplifier is switched
// through the XL9555.
void board_audio_set_muted(bool muted);
void board_speaker_set_enabled(bool enabled);

hal_battery_reading_t board_battery_read(void);

int board_encoder_read(void);
bool board_button_update(uint32_t now); // Encoder push. True once per debounced press.
bool board_button_pressed(void);

// The SI4732 shares the always-powered I2C bus; reset goes through the XL9555.
// begin() starts its 32.768 kHz reference on Si5351 CLK1; reset() retries that if it failed.
bool board_radio_begin(void); // False when the reference clock did not lock.
void board_radio_set_powered(bool powered);
void board_radio_reset(void);
i2c_bus_t board_radio_bus(void);

// Turns off the amplifier, reference clock and display, arms the encoder key
// as the wake source and releases the power latch. On battery the board loses
// power; on external power it deep-sleeps until the key is pressed. Call with
// the key released (otherwise it wakes at once) and settings already saved.
void board_power_off(void) __attribute__((noreturn));

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "drivers/i2c_bus.h"
#include "boards/keys.h"
#include "hal/battery.h"
#include "hal/environment.h"
#define BOARD_NAME "GeekNest V5A"
#define BOARD_STORAGE_NAMESPACE "geeknest-v5a" // Distinct from the original firmware's "radio".
#define BOARD_SCREEN_WIDTH 320
#define BOARD_SCREEN_HEIGHT 240
#define BOARD_HAS_RTC 1 // PCF8523 keeps UTC across power off.
#define BOARD_HAS_AIR 1 // 118-137 MHz through the Si5351 LO and SA602 mixer.
#define BOARD_AUDIO_RATE 16000 // Analog audio sampled for the spectrum display.
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
// Side keys A/B/C. Returns the keys pressed since the last call; *repeats gets
// the keys held long enough to auto-repeat (0.7 s, then every 150 ms).
uint8_t board_keys_read(uint32_t now, uint8_t* repeats);

// PCF8523 calendar in UTC. read() is false when the clock was never set,
// stopped, or reports an oscillator stop since it was last set.
bool board_rtc_read(int64_t* utc_seconds);
bool board_rtc_write(int64_t utc_seconds);
// SHT30, measured every five seconds without blocking. Returns true when
// *reading changed: a new measurement, or a failed one (valid = false).
bool board_environment_poll(uint32_t now, hal_environment_reading_t* reading);
// Demodulated audio (left channel, GPIO34) at BOARD_AUDIO_RATE, 12-bit unsigned.
// Nonblocking: true when a whole block of `count` samples (at most 256) was read.
bool board_audio_read(uint16_t* samples, size_t count);

// The SI4732 shares the always-powered I2C bus; reset goes through the XL9555.
// begin() starts its 32.768 kHz reference on Si5351 CLK1; reset() retries that if it failed.
bool board_radio_begin(void); // False when the reference clock did not lock.
void board_radio_set_powered(bool powered);
void board_radio_reset(void);
i2c_bus_t board_radio_bus(void);
// RF front end. set() routes the antenna for rf_hz (direct for LW/MW, LNA for
// SW/VHF, LNA + SA602 mixer for AIR) and, for AIR, sets the LO to rf - 10.7 MHz.
// receiver_hz() is the frequency the SI4732 must tune to: the 10.7 MHz IF for AIR.
bool board_frontend_set(uint32_t rf_hz);
uint32_t board_frontend_receiver_hz(uint32_t rf_hz);

// Turns off the amplifier, reference clock and display, arms the encoder key
// as the wake source and releases the power latch. On battery the board loses
// power; on external power it deep-sleeps until the key is pressed. Call with
// the key released (otherwise it wakes at once) and settings already saved.
void board_power_off(void) __attribute__((noreturn));

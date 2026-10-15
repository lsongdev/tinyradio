#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "drivers/i2c_bus.h"
#include "tuning.h"
#include "rds.h"
#include "settings.h"
typedef enum {
  SI473X_OK, SI473X_NO_DEVICE, SI473X_BUS_ERROR, SI473X_TIMEOUT,
  SI473X_COMMAND_ERROR, SI473X_BAD_FREQUENCY, SI473X_PATCH_ERROR
} si473x_error_t;
typedef enum {
  SI473X_SEEK_NONE, SI473X_SEEK_SEARCHING, SI473X_SEEK_FOUND,
  SI473X_SEEK_LIMIT, SI473X_SEEK_CANCELLED, SI473X_SEEK_TIMEOUT
} si473x_seek_result_t;
typedef struct {
  uint8_t rssi, snr;
  bool valid, stereo;
  uint8_t blend, gain_index;
  bool pilot, soft_muted, afc_rail, agc_enabled, quality_known;
} si473x_signal_t;
typedef struct {
  bool synchronized;
  uint8_t queued;
  uint32_t groups, overruns;
  uint32_t usable_groups; // Groups with every block correctable (BLE 0..2), regardless of group type.
} si473x_rds_status_t;
typedef struct { uint8_t part, major, minor, component_major, component_minor, chip; uint16_t patch; } si473x_revision_t;
// Fields after `external_clock` are driver state: read them, never write them.
typedef struct {
  i2c_bus_t bus;
  void (*log)(const char* line); // Optional diagnostics, without a trailing newline.
  // False: 32.768 kHz crystal on the chip. True: 32.768 kHz clock driven into RCLK.
  bool external_clock;
  uint8_t address;
  si473x_error_t error;
  si473x_mode_t mode;
  bool powered, configured, tuning, seeking, carrier_known;
  si473x_seek_result_t seek_result;
  uint32_t seek_lower_hz, seek_upper_hz;
  uint16_t carrier;
  uint32_t requested_hz, confirmed_hz, tune_started, last_poll;
  uint32_t bandwidth_hz;
  si473x_signal_t signal;
  si473x_revision_t revision;
  rds_decoder_t rds;
  si473x_rds_status_t rds_status;
} si473x_t;

void si473x_init(si473x_t* chip, i2c_bus_t bus);
// The board must power and reset the device before begin(). Detects both addresses.
bool si473x_begin(si473x_t* chip);
// Leaves audio muted; unmute explicitly after tuning is confirmed.
bool si473x_configure(si473x_t* chip, si473x_mode_t mode, uint8_t volume);
bool si473x_tune(si473x_t* chip, uint32_t hz);
bool si473x_seek(si473x_t* chip, bool up, uint32_t lower_hz, uint32_t upper_hz, uint32_t spacing_hz);
bool si473x_cancel_seek(si473x_t* chip);
// Nonblocking STC polling; confirmed_hz is updated on completion.
bool si473x_poll(si473x_t* chip);
bool si473x_read_signal(si473x_t* chip);
bool si473x_read_rds(si473x_t* chip);
bool si473x_set_reception(si473x_t* chip, const si473x_reception_t* settings);
bool si473x_set_volume(si473x_t* chip, uint8_t volume);
bool si473x_set_muted(si473x_t* chip, bool muted);
// POWER_DOWN; begin() and configure() bring the chip back.
bool si473x_power_down(si473x_t* chip);
const char* si473x_error_text(const si473x_t* chip);

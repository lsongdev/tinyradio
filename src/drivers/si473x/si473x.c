#include "si473x.h"
#include <Arduino.h>
#include <pgmspace.h> // Not pulled in by Arduino.h for C sources.
#include <stdio.h>
#include <string.h>
#include "patches/ssb.h"

static bool fail(si473x_t* chip, si473x_error_t error) {
  rds_decoder_clear(&chip->rds); memset(&chip->rds_status, 0, sizeof chip->rds_status);
  chip->error = error; chip->configured = false; chip->tuning = chip->seeking = false; chip->carrier_known = false;
  return false;
}
static void dump(si473x_t* chip, const char* label, const uint8_t* bytes, size_t size) {
  if (!chip->log) return;
  char line[64]; int used = snprintf(line, sizeof line, "%s", label);
  for (size_t i = 0; i < size && used < (int)sizeof line - 3; ++i)
    used += snprintf(line + used, sizeof line - used, " %02x", bytes[i]);
  chip->log(line);
}
// STC status replies carry RSSI/SNR/VALID; fuller quality needs read_signal().
static void reset_signal(si473x_t* chip, const uint8_t* response) {
  memset(&chip->signal, 0, sizeof chip->signal);
  chip->signal.rssi = response[4]; chip->signal.snr = response[5]; chip->signal.valid = response[1] & 1;
}
void si473x_init(si473x_t* chip, i2c_bus_t bus) {
  memset(chip, 0, sizeof *chip);
  chip->bus = bus; chip->mode = SI473X_FM;
  rds_decoder_clear(&chip->rds);
}
const char* si473x_error_text(const si473x_t* chip) {
  static const char* const names[] = {"OK", "SI4732 not found", "I2C error", "SI4732 timeout",
                                      "Command rejected", "Invalid frequency", "SSB patch mismatch"};
  return names[chip->error];
}
bool si473x_begin(si473x_t* chip) {
  rds_decoder_clear(&chip->rds); memset(&chip->rds_status, 0, sizeof chip->rds_status);
  chip->error = SI473X_OK; chip->powered = chip->configured = chip->tuning = chip->carrier_known = false;
  chip->confirmed_hz = 0; memset(&chip->revision, 0, sizeof chip->revision);
  memset(&chip->signal, 0, sizeof chip->signal); chip->seeking = false; chip->seek_result = SI473X_SEEK_NONE;
  static const uint8_t addresses[] = {0x11, 0x63};
  for (size_t i = 0; i < sizeof addresses; ++i) {
    if (chip->bus.write(chip->bus.context, addresses[i], NULL, 0)) { chip->address = addresses[i]; return true; }
  }
  chip->address = 0; return fail(chip, SI473X_NO_DEVICE);
}
static bool read_bytes(si473x_t* chip, uint8_t* response, uint8_t size) {
  if (!chip->bus.read(chip->bus.context, chip->address, response, size)) return fail(chip, SI473X_BUS_ERROR);
  return true;
}
static bool wait_ready(si473x_t* chip, uint32_t timeout) {
  const uint32_t start = millis();
  do {
    uint8_t status;
    if (!read_bytes(chip, &status, 1)) return false;
    if (status & 0x80) return (status & 0x40) ? fail(chip, SI473X_COMMAND_ERROR) : true;
    delay(1);
  } while (millis() - start < timeout);
  return fail(chip, SI473X_TIMEOUT);
}
static bool command(si473x_t* chip, const uint8_t* bytes, uint8_t size, uint8_t* response, uint8_t response_size) {
  if (!chip->address) return fail(chip, SI473X_NO_DEVICE);
  if (!wait_ready(chip, 500)) return false;
  if (!chip->bus.write(chip->bus.context, chip->address, bytes, size)) return fail(chip, SI473X_BUS_ERROR);
  // Allow the device to deassert CTS for the new command. An immediate read
  // can still return the previous command's CTS/response (including GET_REV).
  delayMicroseconds(300);
  if (!wait_ready(chip, 500)) return false;
  if (response_size) {
    if (!read_bytes(chip, response, response_size)) return false;
    if (response[0] & 0x40) return fail(chip, SI473X_COMMAND_ERROR);
  }
  return true;
}
static bool property(si473x_t* chip, uint16_t id, uint16_t value) {
  const uint8_t bytes[] = {0x12, 0, (uint8_t)(id >> 8), (uint8_t)id, (uint8_t)(value >> 8), (uint8_t)value};
  return command(chip, bytes, sizeof bytes, NULL, 0);
}
enum { XOSCEN = 0x10 };
static uint8_t clock_source(const si473x_t* chip, uint8_t function) {
  return chip->external_clock ? (uint8_t)(function & ~XOSCEN) : (uint8_t)(function | XOSCEN);
}
static bool power_up(si473x_t* chip, uint8_t function) {
  const uint8_t bytes[] = {0x01, clock_source(chip, function), 0x05}; // Analog L/R output
  if (!command(chip, bytes, sizeof bytes, NULL, 0)) return false;
  // AN332 POWER_UP: CTS only confirms command completion. With XOSCEN,
  // allow at least 500 ms for the crystal to settle before the first tune.
  delay(chip->external_clock ? 20 : 500);
  chip->powered = true;
  return true;
}
// REFCLK = RCLK / prescale = 32768 Hz. Valid only after any patch download.
static bool set_reference(si473x_t* chip) {
  return !chip->external_clock || (property(chip, 0x0201, 32768) && property(chip, 0x0202, 1));
}
static bool get_revision(si473x_t* chip) {
  const uint8_t cmd[] = {0x10}; uint8_t response[9];
  if (!command(chip, cmd, 1, response, sizeof response)) return false;
  char label[16]; snprintf(label, sizeof label, "GET_REV @%02x:", chip->address);
  dump(chip, label, response, sizeof response);
  const si473x_revision_t revision = {response[1], response[2], response[3], response[6], response[7], response[8],
                                      (uint16_t)((response[4] << 8) | response[5])};
  chip->revision = revision;
  return true;
}
static bool load_ssb(si473x_t* chip) {
  uint8_t response[8]; const uint8_t query[] = {0x01, clock_source(chip, 0x0f), 0x05};
  if (!command(chip, query, sizeof query, response, sizeof response)) return false;
  dump(chip, "LIBRARY:", response, sizeof response);
  // amrx_6_0_1 is a component version, not a library ID. SI4732-A10
  // can report zero in library/part fields; verify PATCH ID after loading.
  if (chip->log) {
    char line[64];
    snprintf(line, sizeof line, "SSB library: part=%02x library=%02x chip=%02x", response[1], response[7], response[6]);
    chip->log(line);
  }
  // Query-library function returns to power-down automatically.
  if (!power_up(chip, 0x21)) return false; // PATCH + AM_SW_LW
  delay(50);
  size_t index15 = 0;
  for (size_t offset = 0, line = 0; offset < sizeof ssb_patch_content; offset += 7, ++line) {
    uint8_t bytes[8] = {0x16};
    if (index15 < sizeof cmd_0x15 / sizeof cmd_0x15[0] && pgm_read_word(cmd_0x15 + index15) == line) {
      bytes[0] = 0x15; ++index15;
    }
    for (int i = 0; i < 7; ++i) bytes[i + 1] = pgm_read_byte(ssb_patch_content + offset + i);
    if (!command(chip, bytes, sizeof bytes, NULL, 0)) return false;
    delayMicroseconds(300);
  }
  delay(10);
  if (!set_reference(chip) || !get_revision(chip)) return false;
  if (chip->revision.patch != 0xA902) return fail(chip, SI473X_PATCH_ERROR);
  // AUDIOBW=1 (2.2kHz), SBCUTFLT=1, AVCEN=1, AFC disabled.
  return property(chip, 0x0101, 0x9011) && property(chip, 0x3302, 0);
}
static bool clear_rds(si473x_t* chip) {
  rds_decoder_clear(&chip->rds); memset(&chip->rds_status, 0, sizeof chip->rds_status);
  if (chip->mode != SI473X_FM || !chip->configured) return true;
  const uint8_t cmd[] = {0x24, 3}; // INTACK + MTFIFO; discard previous station data.
  return command(chip, cmd, sizeof cmd, NULL, 0);
}
bool si473x_configure(si473x_t* chip, si473x_mode_t mode, uint8_t volume) {
  if (chip->seeking && !si473x_cancel_seek(chip)) return false;
  rds_decoder_clear(&chip->rds); memset(&chip->rds_status, 0, sizeof chip->rds_status);
  const bool same_engine = chip->configured &&
                           (mode == chip->mode || (si473x_is_ssb(mode) && si473x_is_ssb(chip->mode)));
  chip->error = SI473X_OK;
  if (!same_engine) {
    chip->configured = false; chip->tuning = false; chip->carrier_known = false;
    memset(&chip->signal, 0, sizeof chip->signal);
    if (chip->powered) {
      const uint8_t down[] = {0x11};
      if (!command(chip, down, 1, NULL, 0)) return false;
      chip->powered = false; delay(5);
    }
    if (si473x_is_ssb(mode)) {
      if (!load_ssb(chip)) return false;
    } else {
      if (!power_up(chip, mode == SI473X_FM ? 0x00 : 0x01) || !set_reference(chip) || !get_revision(chip)) return false;
      if (mode == SI473X_FM) {
        if (!property(chip, 0x1100, 1) || !property(chip, 0x1102, 0)) return false; // 50us FM de-emphasis
        // Enable RDS, with all corrected blocks (BLE 0..2) admitted to the FIFO.
        if (!property(chip, 0x1500, 1) || !property(chip, 0x1501, 1) || !property(chip, 0x1502, 0xAA01)) return false;
      } else if (!property(chip, 0x3102, 2) || !property(chip, 0x3302, 0)) return false; // AM 3kHz, soft mute off
    }
  }
  chip->mode = mode; chip->bandwidth_hz = mode == SI473X_FM ? 0 : si473x_is_ssb(mode) ? 2200 : 3000;
  chip->carrier_known = false; chip->confirmed_hz = 0;
  if (!si473x_set_volume(chip, volume) || !si473x_set_muted(chip, false)) return false;
  chip->configured = true;
  return true;
}
bool si473x_power_down(si473x_t* chip) {
  if (chip->seeking && !si473x_cancel_seek(chip)) return false;
  rds_decoder_clear(&chip->rds); memset(&chip->rds_status, 0, sizeof chip->rds_status);
  chip->configured = chip->tuning = chip->carrier_known = false;
  if (!chip->powered) return true;
  const uint8_t down[] = {0x11};
  if (!command(chip, down, 1, NULL, 0)) return false;
  chip->powered = false;
  return true;
}
bool si473x_set_volume(si473x_t* chip, uint8_t volume) { return property(chip, 0x4000, volume > 63 ? 63 : volume); }
bool si473x_set_muted(si473x_t* chip, bool muted) { return property(chip, 0x4001, muted ? 3 : 0); }
bool si473x_tune(si473x_t* chip, uint32_t hz) {
  if (!chip->configured || chip->seeking) return false;
  if (!clear_rds(chip)) return false;
  si473x_tune_word_t word;
  if (!si473x_encode_frequency(chip->mode, hz, &word)) return fail(chip, SI473X_BAD_FREQUENCY);
  const bool ssb = si473x_is_ssb(chip->mode), fm = chip->mode == SI473X_FM;
  if (ssb && chip->carrier_known) {
    // Keep the RF carrier anchored within an 8kHz BFO window, avoiding slow
    // carrier settling on every kHz boundary during continuous SSB tuning.
    const int64_t offset = (int64_t)hz - (int64_t)chip->carrier * 1000;
    if (offset >= -8000 && offset <= 8000) {
      if (!property(chip, 0x0100, (uint16_t)-offset)) return false;
      chip->requested_hz = chip->confirmed_hz = hz;
      return true;
    }
  }
  if (ssb && !property(chip, 0x0100, (uint16_t)word.bfo)) return false;
  uint8_t bytes[] = {(uint8_t)(fm ? 0x20 : 0x40), (uint8_t)(ssb ? (chip->mode == SI473X_LSB ? 0x41 : 0x81) : 0),
                     (uint8_t)(word.carrier >> 8), (uint8_t)word.carrier, 0, 0};
  // SW needs fixed antenna capacitor 1; LW/MW use automatic capacitor 0.
  if (!fm && hz >= 1800000) bytes[5] = 1;
  if (!command(chip, bytes, fm ? 5 : 6, NULL, 0)) return false;
  chip->carrier = word.carrier; chip->requested_hz = hz;
  chip->tune_started = chip->last_poll = millis(); chip->tuning = true; chip->carrier_known = false;
  return true;
}
bool si473x_seek(si473x_t* chip, bool up, uint32_t lower_hz, uint32_t upper_hz, uint32_t spacing_hz) {
  if (!chip->configured || chip->tuning || chip->seeking || si473x_is_ssb(chip->mode)) return false;
  if (!clear_rds(chip)) return false;
  si473x_tune_word_t low, high;
  if (lower_hz > upper_hz || !si473x_encode_frequency(chip->mode, lower_hz, &low) ||
      !si473x_encode_frequency(chip->mode, upper_hz, &high))
    return fail(chip, SI473X_BAD_FREQUENCY);
  const bool fm = chip->mode == SI473X_FM;
  const uint16_t base = fm ? 0x1400 : 0x3400;
  uint32_t am_spacing = spacing_hz / 1000;
  if (am_spacing < 1) am_spacing = 1;
  if (am_spacing > 10) am_spacing = 10;
  const uint16_t spacing = fm ? (spacing_hz <= 50000 ? 5 : spacing_hz <= 100000 ? 10 : 20) : (uint16_t)am_spacing;
  if (!property(chip, base, low.carrier) || !property(chip, base + 1, high.carrier) ||
      !property(chip, base + 2, spacing) || !property(chip, base + 3, fm ? 3 : 5) ||
      !property(chip, base + 4, fm ? 20 : 25)) return false;
  // Clear any previous STC before beginning the new search. WRAP stays zero.
  const uint8_t status_cmd[] = {(uint8_t)(fm ? 0x22 : 0x42), 1}; uint8_t response[8];
  if (!command(chip, status_cmd, sizeof status_cmd, response, sizeof response)) return false;
  const uint8_t bytes[] = {(uint8_t)(fm ? 0x21 : 0x41), (uint8_t)(up ? 8 : 0), 0, 0, 0,
                           (uint8_t)(!fm && lower_hz >= 1800000 ? 1 : 0)};
  if (!command(chip, bytes, fm ? 2 : 6, NULL, 0)) return false;
  chip->seek_lower_hz = lower_hz; chip->seek_upper_hz = upper_hz;
  chip->seeking = true; chip->seek_result = SI473X_SEEK_SEARCHING;
  chip->tune_started = chip->last_poll = millis();
  return true;
}
static bool seek_status(si473x_t* chip, bool cancel, bool acknowledge) {
  const bool fm = chip->mode == SI473X_FM;
  const uint8_t cmd[] = {(uint8_t)(fm ? 0x22 : 0x42), (uint8_t)((cancel ? 2 : 0) | (acknowledge ? 1 : 0))};
  uint8_t response[8];
  if (!command(chip, cmd, sizeof cmd, response, sizeof response)) return false;
  const uint16_t actual = (response[2] << 8) | response[3];
  const uint32_t hz = actual * (fm ? 10000u : 1000u);
  if (hz < chip->seek_lower_hz || hz > chip->seek_upper_hz) return fail(chip, SI473X_BAD_FREQUENCY);
  chip->carrier = actual; chip->requested_hz = chip->confirmed_hz = hz; chip->carrier_known = true;
  reset_signal(chip, response);
  if (cancel || (response[0] & 1)) {
    chip->seeking = false;
    if (!clear_rds(chip)) return false;
    chip->seek_result = cancel ? SI473X_SEEK_CANCELLED : (response[1] & 0x80) ? SI473X_SEEK_LIMIT : SI473X_SEEK_FOUND;
  }
  return true;
}
bool si473x_cancel_seek(si473x_t* chip) {
  return !chip->seeking || seek_status(chip, true, true);
}
static bool poll_seek(si473x_t* chip) {
  if (millis() - chip->last_poll < 100) return true;
  chip->last_poll = millis();
  if (chip->last_poll - chip->tune_started >= 30000) {
    if (!si473x_cancel_seek(chip)) return false;
    chip->seek_result = SI473X_SEEK_TIMEOUT;
    return true;
  }
  if (!seek_status(chip, false, false)) return false;
  if (!chip->seeking) {
    const uint8_t ack[] = {(uint8_t)(chip->mode == SI473X_FM ? 0x22 : 0x42), 1};
    if (!command(chip, ack, sizeof ack, NULL, 0)) return false;
  }
  return true;
}
bool si473x_poll(si473x_t* chip) {
  if (chip->seeking) return poll_seek(chip);
  if (!chip->tuning || millis() - chip->last_poll < 5) return chip->error == SI473X_OK;
  chip->last_poll = millis();
  if (chip->last_poll - chip->tune_started > 3000) return fail(chip, SI473X_TIMEOUT);
  uint8_t status; const uint8_t interrupt[] = {0x14};
  if (!command(chip, interrupt, 1, &status, 1)) return false;
  if (!(status & 1)) return true;
  const uint8_t cmd[] = {(uint8_t)(chip->mode == SI473X_FM ? 0x22 : 0x42), 1}; uint8_t response[8];
  if (!command(chip, cmd, sizeof cmd, response, sizeof response)) return false;
  const uint16_t actual = (response[2] << 8) | response[3];
  if (actual != chip->carrier) return fail(chip, SI473X_BAD_FREQUENCY);
  if (!clear_rds(chip)) return false;
  chip->confirmed_hz = chip->requested_hz; chip->tuning = false; chip->carrier_known = true;
  reset_signal(chip, response);
  return true;
}
bool si473x_set_reception(si473x_t* chip, const si473x_reception_t* settings) {
  int count; const si473x_bandwidth_t* options = si473x_bandwidths(chip->mode, &count);
  if (!chip->configured || chip->tuning || chip->seeking || settings->bandwidth >= count || settings->gain < -1 ||
      settings->gain > si473x_max_gain_index(chip->mode) || settings->soft_mute_db > 31) return false;
  const si473x_bandwidth_t* bw = &options[settings->bandwidth];
  const bool ssb = si473x_is_ssb(chip->mode), fm = chip->mode == SI473X_FM;
  const uint16_t bw_value = ssb ? (uint16_t)(0x9000 | (bw->hz > 2000 ? 0x10 : 0) | bw->code) : bw->code;
  if (!property(chip, fm ? 0x1102 : ssb ? 0x0101 : 0x3102, bw_value) ||
      !property(chip, fm ? 0x1302 : 0x3302, settings->soft_mute_db)) return false;
  const uint8_t agc[] = {(uint8_t)(fm ? 0x28 : 0x48), (uint8_t)(settings->gain >= 0),
                         (uint8_t)(settings->gain < 0 ? 0 : settings->gain)};
  if (!command(chip, agc, sizeof agc, NULL, 0)) return false;
  chip->bandwidth_hz = bw->hz; chip->signal.quality_known = false;
  return true;
}
bool si473x_read_rds(si473x_t* chip) {
  if (!chip->configured || chip->mode != SI473X_FM || chip->tuning || chip->seeking) return true;
  uint8_t response[13]; const uint8_t peek[] = {0x24, 5}; // STATUSONLY + INTACK.
  if (!command(chip, peek, sizeof peek, response, sizeof response)) return false;
  chip->rds_status.synchronized = response[2] & 1;
  chip->rds_status.queued = response[3];
  // GRPLOST reports discarded groups, not corrupt data in the surviving FIFO.
  // Drain those groups instead of flushing them; block errors and PI validation
  // in the decoder protect message assembly even after a synchronization gap.
  if (response[2] & 4) ++chip->rds_status.overruns;
  const uint8_t count = response[3] < 8 ? response[3] : 8;
  const uint8_t pop[] = {0x24, 1};
  for (uint8_t i = 0; i < count; ++i) {
    if (!command(chip, pop, sizeof pop, response, sizeof response)) return false;
    ++chip->rds_status.groups;
    rds_decoder_accept(&chip->rds, (uint16_t)(response[4] << 8 | response[5]), (uint16_t)(response[6] << 8 | response[7]),
                       (uint16_t)(response[10] << 8 | response[11]), response[12], (uint16_t)(response[8] << 8 | response[9]));
  }
  return true;
}
bool si473x_read_signal(si473x_t* chip) {
  if (!chip->configured || chip->tuning || chip->seeking) return false;
  const bool fm = chip->mode == SI473X_FM;
  const uint8_t cmd[] = {(uint8_t)(fm ? 0x23 : 0x43), 0}; uint8_t response[8];
  if (!command(chip, cmd, sizeof cmd, response, fm ? 8 : 6)) return false;
  si473x_signal_t* signal = &chip->signal;
  signal->rssi = response[4]; signal->snr = response[5]; signal->valid = response[2] & 1;
  signal->blend = fm ? (response[3] & 0x7f) : 0;
  signal->stereo = fm && signal->blend > 0;
  signal->pilot = fm && (response[3] & 0x80);
  signal->soft_muted = response[2] & 8; signal->afc_rail = response[2] & 2;
  signal->quality_known = false;
  const uint8_t agc_cmd[] = {(uint8_t)(fm ? 0x27 : 0x47)}; uint8_t agc[3];
  if (!command(chip, agc_cmd, sizeof agc_cmd, agc, sizeof agc)) return false;
  signal->agc_enabled = !(agc[1] & 1); signal->gain_index = agc[2]; signal->quality_known = true;
  return true;
}

#include "receiver.h"
#include "rds_clock.h"
#include "boards/board.h"
#include "drivers/si473x/si473x.h"
#include "hal/clock.h"
#include "hal/serial.h"
#include "hal/settings_storage.h"
#include "radio/model.h"
#include "radio/saved_settings.h"
#include "services/device_clock.h"
#include "ui/ui.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

static hal_battery_reading_t supply;
static si473x_t chip;
static radio_model_t model;
static device_clock_t device_clock;
static rds_clock_sync_t rds_clock_sync;
static ui_field_t field = UI_FIELD_FREQUENCY;
static int volume = 18, applied_volume = -1;
static int brightness = 128;
static si473x_reception_t reception[4] = {{0, -1, 16}, {4, -1, 0}, {3, -1, 0}, {3, -1, 0}};
static bool reception_dirty = true;
static hal_settings_storage_t storage;
static uint8_t observed_settings[RADIO_SETTINGS_SIZE], stored_settings[RADIO_SETTINGS_SIZE];
static bool storage_ready = false, settings_stored = false, save_pending = false;
static uint32_t last_settings_change = 0, last_save_attempt = 0, settings_writes = 0;
static bool muted = false, dirty = true, lcd = false, buffered = false, failed = false;
static bool screenshot_requested = false;
static bool reconfigure = true, held = false, previous_pressed = false, audio_ready = false, seek_mode = false;
static int pending_seek = 0, seek_direction = 0;
static uint32_t pressed_at = 0, edited_at = 0, last_signal = 0, last_battery = 0, last_draw = 0, last_tune = 0, last_rds = 0;

static int clamp(int value, int low, int high) { return value < low ? low : value > high ? high : value; }
static si473x_reception_t* settings(void) { return &reception[radio_model_mode(&model)]; }
static radio_settings_t saved_settings(void) {
  radio_settings_t saved;
  radio_settings_default(&saved);
  saved.tuning = radio_model_state(&model);
  saved.volume = volume; saved.brightness = brightness; saved.muted = muted;
  memcpy(saved.reception, reception, sizeof reception);
  return saved;
}
static bool encode_current(uint8_t bytes[RADIO_SETTINGS_SIZE]) {
  const radio_settings_t saved = saved_settings();
  return radio_settings_encode(&saved, bytes);
}
static void load_settings(void) {
  storage_ready = hal_settings_begin(&storage, BOARD_STORAGE_NAMESPACE);
  uint8_t bytes[RADIO_SETTINGS_SIZE];
  radio_settings_t saved;
  if (storage_ready && hal_settings_load(&storage, bytes, sizeof bytes) && radio_settings_decode(bytes, &saved)) {
    radio_model_restore(&model, &saved.tuning); volume = saved.volume; brightness = saved.brightness; muted = saved.muted;
    memcpy(reception, saved.reception, sizeof reception);
    settings_stored = true;
  }
  encode_current(observed_settings); memcpy(stored_settings, observed_settings, sizeof stored_settings);
  serial_printf("Settings: %s\n", !storage_ready ? "NVS unavailable" : settings_stored ? "restored" : "defaults");
}
static void remember_settings(uint32_t now) {
  uint8_t bytes[RADIO_SETTINGS_SIZE];
  if (!storage_ready || !encode_current(bytes)) return;
  if (memcmp(bytes, observed_settings, sizeof bytes)) {
    memcpy(observed_settings, bytes, sizeof bytes); last_settings_change = now;
    save_pending = !settings_stored || memcmp(observed_settings, stored_settings, sizeof bytes);
  }
  if (!save_pending || now - last_settings_change < 2000 || chip.tuning || chip.seeking ||
      pending_seek || (reconfigure && !failed) || (last_save_attempt && now - last_save_attempt < 10000)) return;
  last_save_attempt = now;
  if (hal_settings_save(&storage, observed_settings, sizeof observed_settings)) {
    memcpy(stored_settings, observed_settings, sizeof stored_settings); settings_stored = true; save_pending = false;
    last_save_attempt = 0; ++settings_writes; serial_println("Settings: saved");
  } else serial_println("Settings: save failed; retry in 10s");
}
static const char* radio_message(void) {
  return rds_enhanced_text(&chip.rds)[0] ? rds_enhanced_text(&chip.rds) : rds_radio_text(&chip.rds);
}
static bool format_clock(char text[6], uint64_t now) {
  const device_time_t time = device_clock_read(&device_clock, now);
  if (time.valid) snprintf(text, 6, "%02u:%02u", time.hour % 24u, time.minute % 60u);
  else memcpy(text, "--:--", 6);
  return time.valid;
}
static void report(void) {
  const si473x_signal_t* s = &chip.signal;
  const si473x_reception_t* r = settings();
  char clock_text[6]; format_clock(clock_text, hal_monotonic_ms());
  const rds_clock_time_t* ct = rds_clock_time(&chip.rds);
  const device_time_t time = device_clock_read(&device_clock, hal_monotonic_ms());
  serial_printf("RADIO band=%s mode=%s requested=%lu confirmed=%lu step=%lu volume=%d brightness=%d mute=%d stored=%d save=%d writes=%lu tuning=%d RSSI=%u SNR=%u valid=%d blend=%u pilot=%d agc=%d gain=%u sm=%d afc=%d bw=%lu focus=%u seek=%d searching=%d result=%u bwsel=%u gainset=%d smset=%u ps=[%s] rt=[%s] ert=[%s] ertgroup=%d ertflags=%u clock=[%s] ctmjd=%lu ctutc=%u ctoffset=%d clocksource=%s clockutc=%lld clockoffset=%d rdssync=%d rdsfifo=%u rdsgroups=%lu rdsoverruns=%lu supply=%.3f adc=%lu error=%s\n",
    radio_model_band(&model)->name, si473x_mode_name(radio_model_mode(&model)), (unsigned long)radio_model_frequency(&model),
    (unsigned long)chip.confirmed_hz, (unsigned long)radio_model_step(&model), volume, brightness, muted, settings_stored, save_pending, (unsigned long)settings_writes,
    chip.tuning, s->rssi, s->snr, s->valid, s->blend, s->pilot, s->agc_enabled, s->gain_index, s->soft_muted, s->afc_rail, (unsigned long)chip.bandwidth_hz, (unsigned)field, seek_mode, chip.seeking, (unsigned)chip.seek_result, r->bandwidth, r->gain, r->soft_mute_db, rds_ps(&chip.rds), rds_radio_text(&chip.rds), rds_enhanced_text(&chip.rds), rds_enhanced_group(&chip.rds), rds_enhanced_flags(&chip.rds), clock_text, (unsigned long)ct->mjd, ct->utc_minutes, ct->offset_minutes, time_source_name(time.source), (long long)time.utc_seconds, time.utc_offset_minutes, chip.rds_status.synchronized, chip.rds_status.queued, (unsigned long)chip.rds_status.groups, (unsigned long)chip.rds_status.overruns, supply.volts, (unsigned long)supply.adc_millivolts, si473x_error_text(&chip));
}
static void draw(void) {
  char clock_text[6];
  const bool clock_valid = format_clock(clock_text, hal_monotonic_ms());
  ui_view_t view;
  memset(&view, 0, sizeof view);
  view.model = &model; view.field = field;
  view.volume = volume; view.brightness = brightness; view.muted = muted;
  view.supply = supply; view.clock = clock_valid ? clock_text : NULL;
  view.signal = chip.signal; view.reception = *settings();
  view.ps = rds_ps(&chip.rds); view.message = radio_message(); view.error = si473x_error_text(&chip);
  view.failed = failed; view.seeking = chip.seeking; view.seek_mode = seek_mode;
  view.pending = reconfigure || chip.tuning || chip.seeking || chip.confirmed_hz != radio_model_frequency(&model);
  view.seek_arrow = seek_mode && (chip.seeking || pending_seek) ? (pending_seek ? pending_seek : seek_direction) : 0;
  view.audio_status_known = chip.signal.quality_known && !reconfigure && !chip.tuning && !chip.seeking;
  ui_draw(&view);
}
static void fail(void) {
  failed = true; seek_mode = false; pending_seek = seek_direction = 0; audio_ready = false;
  board_audio_set_muted(true); board_speaker_set_enabled(false);
  dirty = true; report();
}
static void changed_configuration(void) {
  if (chip.seeking && !si473x_cancel_seek(&chip)) fail();
  seek_mode = false; pending_seek = seek_direction = 0;
  reconfigure = true; edited_at = millis(); dirty = true;
}
static void retry(void) {
  board_audio_set_muted(true); board_speaker_set_enabled(false);
  board_radio_reset(); failed = !si473x_begin(&chip); applied_volume = -1;
  audio_ready = false; changed_configuration();
}
static void tune_by(int delta) {
  const size_t before = model.band;
  radio_model_turn_frequency(&model, delta);
  if (before != model.band) changed_configuration();
  dirty = true;
}
static void stop_seek(bool manual) {
  pending_seek = seek_direction = 0;
  if (chip.seeking) {
    if (!si473x_cancel_seek(&chip)) fail();
    else radio_model_set_frequency(&model, chip.confirmed_hz);
  }
  if (manual) seek_mode = false;
  dirty = true;
}
static void request_seek(int delta) {
  if (failed || si473x_is_ssb(radio_model_mode(&model)) || !delta) return;
  const int direction = delta > 0 ? 1 : -1;
  if (chip.seeking && direction == seek_direction) return; // No queued station skipping.
  if (chip.seeking) stop_seek(false);
  pending_seek = direction; dirty = true;
}
static void adjust_frequency(int delta) {
  if (seek_mode) request_seek(delta); else tune_by(delta);
}
static void toggle_mute(void) {
  muted = !muted; if (audio_ready) board_audio_set_muted(muted); dirty = true;
}
static void long_press(void) {
  if (field == UI_FIELD_FREQUENCY) {
    if (seek_mode) stop_seek(true);
    else if (!failed && !si473x_is_ssb(radio_model_mode(&model))) { seek_mode = true; dirty = true; }
  } else if (field == UI_FIELD_VOLUME) toggle_mute();
}
static void short_press(void) {
  if (failed) retry();
  else if (seek_mode || chip.seeking || pending_seek) stop_seek(true);
  else { field = (ui_field_t)((field + 1) % UI_FIELD_COUNT); dirty = true; }
}
static void turn(int delta) {
  const si473x_mode_t mode = radio_model_mode(&model);
  si473x_reception_t* r = settings();
  switch (field) {
    case UI_FIELD_FREQUENCY: adjust_frequency(delta); break;
    case UI_FIELD_BAND: radio_model_turn_band(&model, delta); changed_configuration(); break;
    case UI_FIELD_MODE: radio_model_turn_mode(&model, delta); changed_configuration(); break;
    case UI_FIELD_STEP: radio_model_turn_step(&model, delta); break;
    case UI_FIELD_VOLUME: volume = clamp(volume + delta, 0, 63); break;
    case UI_FIELD_BANDWIDTH: {
      int count; si473x_bandwidths(mode, &count);
      r->bandwidth = (r->bandwidth + delta % count + count) % count; reception_dirty = true; break;
    }
    case UI_FIELD_GAIN: r->gain = clamp(r->gain + delta, -1, si473x_max_gain_index(mode)); reception_dirty = true; break;
    case UI_FIELD_SOFT_MUTE: r->soft_mute_db = clamp(r->soft_mute_db + delta, 0, 31); reception_dirty = true; break;
    case UI_FIELD_BRIGHTNESS:
      brightness = clamp(brightness + delta * 8, 8, 255);
      if (lcd) board_display_set_brightness(brightness);
      break;
    case UI_FIELD_COUNT: break;
  }
  dirty = true;
}
// Single-character diagnostics for repeatable real-device checks.
static void serial_input(void) {
  for (int count = 0; count < 16; ++count) {
    const int c = serial_read();
    if (c < 0) break;
    if (c >= '0' && c <= '3') { radio_model_set_band(&model, c - '0'); changed_configuration(); }
    else if (c == 'a' || c == 'l' || c == 'u' || c == 'f') {
      const si473x_mode_t mode = c == 'a' ? SI473X_AM : c == 'l' ? SI473X_LSB : c == 'u' ? SI473X_USB : SI473X_FM;
      if (radio_model_set_mode(&model, mode)) changed_configuration();
      else serial_println("Mode unavailable on this band");
    } else if (c == '+' || c == '-') adjust_frequency(c == '+' ? 1 : -1);
    else if (c == '[' || c == ']') {
      const radio_band_t* band = radio_model_band(&model);
      stop_seek(true); radio_model_set_frequency(&model, c == '[' ? band->min_hz : band->max_hz); dirty = true;
    }
    else if (c == 'm') toggle_mute();
    else if (c == 'e') short_press();
    else if (c == 'h') long_press();
    else if (c == '>') turn(1);
    else if (c == '<') turn(-1);
    else if (c == '?') report();
    else if (c == 'S') screenshot_requested = true;
    else if (c == 'r') retry();
  }
}
static void log_line(const char* line) { serial_println(line); }
static void button_input(uint32_t now) {
  board_button_update(now);
  const bool pressed = board_button_pressed();
  if (pressed && !previous_pressed) {
    pressed_at = now; held = false;
    if (chip.seeking || pending_seek) { stop_seek(true); held = true; }
  }
  if (pressed && !held && now - pressed_at >= 650) {
    held = true; long_press();
  }
  if (!pressed && previous_pressed && !held) short_press();
  previous_pressed = pressed;
}

void receiver_begin(void) {
  serial_begin(115200); board_begin();
  radio_model_init(&model);
  load_settings();
  lcd = board_display_begin();
  if (lcd) { board_display_set_brightness(brightness); buffered = ui_begin(); }
  supply = board_battery_read();
  const bool radio_ready = board_radio_begin();
  board_radio_set_powered(true); board_radio_reset();
  si473x_init(&chip, board_radio_bus());
  chip.log = log_line;
  failed = !si473x_begin(&chip);
  serial_printf("Receiver boot: board=%s LCD=%d framebuffer=%d largestblock=%u radiohw=%d I2C=0x%02x found=%d\n", BOARD_NAME, lcd, buffered,
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), radio_ready, chip.address, !failed);
  edited_at = millis() - 200;
  if (failed) fail();
}
void receiver_update(void) {
  const uint32_t now = millis();
  button_input(now);
  const int delta = board_encoder_read(); if (delta) turn(delta);
  serial_input();
  if (!failed && reconfigure && now - edited_at >= 180) {
    board_audio_set_muted(true); audio_ready = false;
    if (!si473x_configure(&chip, radio_model_mode(&model), volume)) fail();
    else {
      const si473x_revision_t* revision = &chip.revision;
      reconfigure = false; applied_volume = volume; reception_dirty = true;
      serial_printf("Configured mode=%s part=%02x fw=%02x.%02x component=%c.%c patch=0x%04x\n", si473x_mode_name(radio_model_mode(&model)), revision->part,
        revision->major, revision->minor, revision->component_major, revision->component_minor, revision->patch);
    }
    dirty = true;
  }
  if (!failed && !reconfigure) {
    const bool was_tuning = chip.tuning, was_seeking = chip.seeking;
    if (!si473x_poll(&chip)) fail();
    if (!failed && was_seeking) {
      radio_model_set_frequency(&model, chip.confirmed_hz); dirty = true;
      if (!chip.seeking) { seek_direction = 0; report(); }
    }
    if (was_tuning && !chip.tuning) { dirty = true; report(); }
    if (!failed && reception_dirty && !chip.tuning && !chip.seeking) {
      if (!si473x_set_reception(&chip, settings())) fail(); else reception_dirty = false;
      dirty = true;
    }
    if (!failed && pending_seek && !chip.tuning && !chip.seeking && chip.confirmed_hz == radio_model_frequency(&model)) {
      const radio_band_t* band = radio_model_band(&model);
      seek_direction = pending_seek; pending_seek = 0;
      board_audio_set_muted(true); audio_ready = false;
      if (!si473x_seek(&chip, seek_direction > 0, band->min_hz, band->max_hz, radio_model_step(&model))) fail();
      dirty = true;
    }
    if (!failed && !chip.tuning && !chip.seeking && chip.confirmed_hz != radio_model_frequency(&model) && now - last_tune >= 30) {
      last_tune = now;
      if (!si473x_tune(&chip, radio_model_frequency(&model))) fail();
      dirty = true;
    }
    if (!failed && !chip.tuning && !chip.seeking && !pending_seek && chip.confirmed_hz == radio_model_frequency(&model) && !audio_ready) {
      audio_ready = true; board_speaker_set_enabled(true); board_audio_set_muted(muted); dirty = true;
    }
    if (!failed && applied_volume != volume) {
      if (!si473x_set_volume(&chip, volume)) fail(); else applied_volume = volume;
    }
    if (!failed && !chip.tuning && !chip.seeking && now - last_rds >= 100) {
      last_rds = now; if (!si473x_read_rds(&chip)) fail(); dirty = true;
    }
    if (!failed && !chip.tuning && !chip.seeking && now - last_signal >= 500) {
      last_signal = now; if (!si473x_read_signal(&chip)) fail(); dirty = true;
    }
  }
  if (now - last_battery >= 1000) { last_battery = now; supply = board_battery_read(); dirty = true; }
  if (radio_message()[0] && now - last_draw >= 50) dirty = true;
  rds_clock_sync_update(&rds_clock_sync, rds_clock_time(&chip.rds), &device_clock, hal_monotonic_ms());
  remember_settings(now);
  if (dirty && now - last_draw >= 50) { dirty = false; last_draw = now; if (lcd) draw(); }
  if (screenshot_requested && (last_draw || !lcd || !buffered)) {
    screenshot_requested = false;
    if (!lcd) serial_println("SCREENSHOT_ERROR framebuffer unavailable");
    else ui_capture();
  }
  delay(1);
}

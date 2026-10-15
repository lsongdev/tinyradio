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
#include "services/spectrum.h"
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
static ui_theme_id_t theme = UI_THEME_DARK;
static si473x_reception_t reception[4] = {{0, -1, 16}, {4, -1, 0}, {3, -1, 0}, {3, -1, 0}};
static bool reception_dirty = true;
static bool band_locked = false; // Session only; not saved.
static bool battery_volts = false; // Session only; not saved.
static uint32_t frontend_hz = 0;  // RF the board front end is set for; 0 forces a new setting.
static bool frontend_failed = false;
static hal_settings_storage_t storage;
static const char SETTINGS_KEY[] = "settings";
// UTC offset in minutes, 2 bytes little-endian; only kept on boards with an RTC.
static const char OFFSET_KEY[] = "utcoffset";
static int16_t stored_offset = 0;
static hal_environment_reading_t environment;
static bool environment_known = false;
static uint8_t spectrum[SPECTRUM_BARS];
static bool spectrum_known = false;
static uint32_t last_spectrum = 0;
static uint8_t observed_settings[RADIO_SETTINGS_SIZE], stored_settings[RADIO_SETTINGS_SIZE];
static bool storage_ready = false, settings_stored = false, save_pending = false;
static uint32_t last_settings_change = 0, last_save_attempt = 0, settings_writes = 0;
static bool muted = false, dirty = true, lcd = false, buffered = false, failed = false;
static bool screenshot_requested = false;
static bool reconfigure = true, held = false, previous_pressed = false, audio_ready = false, seek_mode = false;
static int pending_seek = 0, seek_direction = 0;
static bool long_pending = false, power_pending = false;
enum { LONG_PRESS_MS = 650, POWER_OFF_MS = 3000 }; // Power off well apart from long-press actions.
static uint32_t pressed_at = 0, edited_at = 0, last_signal = 0, last_battery = 0, last_draw = 0, last_tune = 0, last_rds = 0;

static int clamp(int value, int low, int high) { return value < low ? low : value > high ? high : value; }
static si473x_reception_t* settings(void) { return &reception[radio_model_mode(&model)]; }
static radio_settings_t saved_settings(void) {
  radio_settings_t saved;
  radio_settings_default(&saved);
  saved.tuning = radio_model_state(&model);
  saved.volume = volume; saved.brightness = brightness; saved.muted = muted;
  saved.light_theme = theme == UI_THEME_LIGHT;
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
  if (storage_ready && hal_settings_load(&storage, SETTINGS_KEY, bytes, sizeof bytes) && radio_settings_decode(bytes, &saved)) {
    radio_model_restore(&model, &saved.tuning); volume = saved.volume; brightness = saved.brightness; muted = saved.muted;
    theme = saved.light_theme ? UI_THEME_LIGHT : UI_THEME_DARK;
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
  if (hal_settings_save(&storage, SETTINGS_KEY, observed_settings, sizeof observed_settings)) {
    memcpy(stored_settings, observed_settings, sizeof stored_settings); settings_stored = true; save_pending = false;
    last_save_attempt = 0; ++settings_writes; serial_println("Settings: saved");
  } else serial_println("Settings: save failed; retry in 10s");
}
#if BOARD_CAN_POWER_OFF
// Writes changed settings now, ignoring the debounce; for power off.
static bool flush_settings(void) {
  uint8_t bytes[RADIO_SETTINGS_SIZE];
  if (!storage_ready) return true; // Nothing can be saved; do not trap the user.
  if (!encode_current(bytes)) return false;
  if (settings_stored && !memcmp(bytes, stored_settings, sizeof bytes)) return true;
  if (!hal_settings_save(&storage, SETTINGS_KEY, bytes, sizeof bytes)) return false;
  memcpy(stored_settings, bytes, sizeof bytes); memcpy(observed_settings, bytes, sizeof bytes);
  settings_stored = true; save_pending = false; ++settings_writes;
  return true;
}
#endif
// At the model frequency: front end set for it and the chip confirmed at the
// frequency the board needs (the IF on converted bands).
static bool on_frequency(void) {
  const uint32_t rf = radio_model_frequency(&model);
  return frontend_hz == rf && chip.confirmed_hz == board_frontend_receiver_hz(rf);
}
static uint32_t confirmed_rf(void) { return on_frequency() ? radio_model_frequency(&model) : chip.confirmed_hz; }
// The chip's own seek only covers frequencies it tunes directly, and not SSB.
static bool can_seek(void) {
  return !si473x_is_ssb(radio_model_mode(&model)) && !radio_model_band(&model)->converted;
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
  serial_printf("RADIO band=%s mode=%s requested=%lu confirmed=%lu step=%lu volume=%d brightness=%d theme=%s mute=%d stored=%d save=%d writes=%lu tuning=%d RSSI=%u SNR=%u valid=%d blend=%u pilot=%d agc=%d gain=%u sm=%d afc=%d bw=%lu focus=%u seek=%d searching=%d result=%u bwsel=%u gainset=%d smset=%u ps=[%s] rt=[%s] ert=[%s] ertgroup=%d ertflags=%u clock=[%s] ctmjd=%lu ctutc=%u ctoffset=%d clocksource=%s clockutc=%lld clockoffset=%d rdssync=%d rdsfifo=%u rdsgroups=%lu rdsoverruns=%lu supply=%.3f adc=%lu locked=%d env=%d temp=%ld rh=%lu error=%s\n",
    radio_model_band(&model)->name, si473x_mode_name(radio_model_mode(&model)), (unsigned long)radio_model_frequency(&model),
    (unsigned long)confirmed_rf(), (unsigned long)radio_model_step(&model), volume, brightness, theme == UI_THEME_LIGHT ? "light" : "dark", muted, settings_stored, save_pending, (unsigned long)settings_writes,
    chip.tuning, s->rssi, s->snr, s->valid, s->blend, s->pilot, s->agc_enabled, s->gain_index, s->soft_muted, s->afc_rail, (unsigned long)chip.bandwidth_hz, (unsigned)field, seek_mode, chip.seeking, (unsigned)chip.seek_result, r->bandwidth, r->gain, r->soft_mute_db, rds_ps(&chip.rds), rds_radio_text(&chip.rds), rds_enhanced_text(&chip.rds), rds_enhanced_group(&chip.rds), rds_enhanced_flags(&chip.rds), clock_text, (unsigned long)ct->mjd, ct->utc_minutes, ct->offset_minutes, time_source_name(time.source), (long long)time.utc_seconds, time.utc_offset_minutes, chip.rds_status.synchronized, chip.rds_status.queued, (unsigned long)chip.rds_status.groups, (unsigned long)chip.rds_status.overruns, supply.volts, (unsigned long)supply.adc_millivolts, band_locked, environment.valid, (long)environment.milli_celsius, (unsigned long)environment.milli_percent, frontend_failed ? "RF front end failed" : si473x_error_text(&chip));
}
// extras_only redraws just the board extras area (spectrum updates).
static void draw(bool extras_only) {
  char clock_text[6], date_text[24];
  const bool clock_valid = format_clock(clock_text, hal_monotonic_ms());
  const device_time_t time = device_clock_read(&device_clock, hal_monotonic_ms());
  if (time.valid)
    snprintf(date_text, sizeof date_text, "%04d-%02u-%02u %02u:%02u:%02u", (int)(time.year % 10000), time.month % 13u,
             time.day % 32u, time.hour % 24u, time.minute % 60u, time.second % 60u);
  ui_view_t view;
  memset(&view, 0, sizeof view);
  view.model = &model; view.field = field; view.theme = theme;
  view.volume = volume; view.brightness = brightness; view.muted = muted;
  view.supply = supply; view.battery_volts = battery_volts; view.clock = clock_valid ? clock_text : NULL;
  view.date_time = time.valid ? date_text : NULL;
  view.environment = environment; view.environment_known = environment_known;
  view.spectrum = spectrum_known ? spectrum : NULL;
  view.signal = chip.signal; view.reception = *settings();
  view.ps = rds_ps(&chip.rds); view.message = radio_message(); view.error = frontend_failed ? "RF front end failed" : si473x_error_text(&chip);
  view.failed = failed; view.seeking = chip.seeking; view.seek_mode = seek_mode; view.band_locked = band_locked;
  view.pending = reconfigure || chip.tuning || chip.seeking || !on_frequency();
  view.seek_arrow = seek_mode && (chip.seeking || pending_seek) ? (pending_seek ? pending_seek : seek_direction) : 0;
  view.audio_status_known = chip.signal.quality_known && !reconfigure && !chip.tuning && !chip.seeking;
  view.notice = power_pending ? "Release to power off" : NULL;
  if (extras_only) ui_draw_extras(&view); else ui_draw(&view);
}
static void fail(void) {
  failed = true; seek_mode = false; pending_seek = seek_direction = 0; audio_ready = false;
  board_audio_set_muted(true); board_speaker_set_enabled(false);
  dirty = true; report();
}
static bool set_audio_muted(bool value) {
  // Some boards have no mute GPIO. Always mute the receiver too, including
  // its headphone outputs, without changing the user's chosen volume.
  board_audio_set_muted(value);
  if (!si473x_set_muted(&chip, value)) { fail(); return false; }
  return true;
}
static void changed_configuration(void) {
  if (chip.seeking && !si473x_cancel_seek(&chip)) fail();
  seek_mode = false; pending_seek = seek_direction = 0;
  reconfigure = true; edited_at = millis(); dirty = true;
}
static void retry(void) {
  board_audio_set_muted(true); board_speaker_set_enabled(false);
  board_radio_reset(); failed = !si473x_begin(&chip); applied_volume = -1;
  frontend_hz = 0; frontend_failed = false;
  audio_ready = false; changed_configuration();
}
static void tune_by(int delta) {
  const size_t before = model.band;
  if (band_locked) radio_model_turn_frequency_in_band(&model, delta);
  else radio_model_turn_frequency(&model, delta);
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
  if (failed || !can_seek() || !delta) return;
  const int direction = delta > 0 ? 1 : -1;
  if (chip.seeking && direction == seek_direction) return; // No queued station skipping.
  if (chip.seeking) stop_seek(false);
  pending_seek = direction; dirty = true;
}
static void adjust_frequency(int delta) {
  if (seek_mode) request_seek(delta); else tune_by(delta);
}
static void toggle_mute(void) {
  muted = !muted; if (audio_ready) set_audio_muted(muted); dirty = true;
}
static void long_press(void) {
  if (field == UI_FIELD_FREQUENCY) {
    if (seek_mode) stop_seek(true);
    else if (!failed && can_seek()) { seek_mode = true; dirty = true; }
  } else if (field == UI_FIELD_VOLUME) toggle_mute();
  else if (field == UI_FIELD_BRIGHTNESS) { theme = theme == UI_THEME_DARK ? UI_THEME_LIGHT : UI_THEME_DARK; dirty = true; }
  else if (field == UI_FIELD_BAND) { band_locked = !band_locked; dirty = true; }
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
    case UI_FIELD_BATTERY: if (delta % 2) battery_volts = !battery_volts; break;
    case UI_FIELD_COUNT: break;
  }
  dirty = true;
}
// Single-character diagnostics for repeatable real-device checks.
static void serial_input(void) {
  for (int count = 0; count < 16; ++count) {
    const int c = serial_read();
    if (c < 0) break;
    if (c >= '0' && c <= '4') { radio_model_set_band(&model, c - '0'); changed_configuration(); }
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
// RTC boards keep UTC in the RTC and the RDS offset in NVS, so local time
// survives power off. RDS CT corrects both.
static void restore_clock(void) {
  uint8_t bytes[2];
  int64_t utc;
  if (storage_ready && hal_settings_load(&storage, OFFSET_KEY, bytes, sizeof bytes)) {
    const int16_t offset = (int16_t)(bytes[0] | bytes[1] << 8);
    if (offset >= -16 * 60 && offset <= 16 * 60 && offset % 30 == 0) stored_offset = offset;
  }
  device_clock_set_utc_offset(&device_clock, stored_offset);
  if (board_rtc_read(&utc)) device_clock_synchronize(&device_clock, utc, TIME_SOURCE_RTC, hal_monotonic_ms());
}
static void store_clock(void) {
  const device_time_t time = device_clock_read(&device_clock, hal_monotonic_ms());
  if (!time.valid) return;
  if (!board_rtc_write(time.utc_seconds)) serial_println("Clock: RTC write failed");
  if (time.utc_offset_minutes != stored_offset) {
    const uint8_t bytes[] = {(uint8_t)time.utc_offset_minutes, (uint8_t)((uint16_t)time.utc_offset_minutes >> 8)};
    if (hal_settings_save(&storage, OFFSET_KEY, bytes, sizeof bytes)) stored_offset = time.utc_offset_minutes;
  }
}
// Current side-key binding: A acts as an encoder short press; B and C step the
// volume up and down, repeating while held.
static void key_input(uint32_t now) {
  uint8_t repeats;
  const uint8_t pressed = board_keys_read(now, &repeats);
  const uint8_t volume_keys = (pressed | repeats) & (BOARD_KEY_B | BOARD_KEY_C);
  if (pressed & BOARD_KEY_A) short_press();
  if (volume_keys) { volume = clamp(volume + (volume_keys & BOARD_KEY_B ? 2 : -2), 0, 63); dirty = true; }
}
#if BOARD_CAN_POWER_OFF
static void power_off(void) {
  if (!flush_settings()) { serial_println("Power off cancelled: settings not saved"); dirty = true; return; }
  serial_println("Power off");
  board_audio_set_muted(true);
  si473x_power_down(&chip);
  board_power_off();
}
#endif
static void button_input(uint32_t now) {
  board_button_update(now);
  const bool pressed = board_button_pressed();
  if (pressed && !previous_pressed) {
    pressed_at = now; held = long_pending = power_pending = false;
    if (chip.seeking || pending_seek) { stop_seek(true); held = true; }
  }
  if (pressed && !held) {
#if BOARD_CAN_POWER_OFF
    // Long-press actions wait for release so that a longer hold can power off.
    if (now - pressed_at >= POWER_OFF_MS) { if (!power_pending) { power_pending = true; dirty = true; } }
    else if (now - pressed_at >= LONG_PRESS_MS) long_pending = true;
#else
    if (now - pressed_at >= LONG_PRESS_MS) { held = true; long_press(); }
#endif
  }
  if (!pressed && previous_pressed && !held) {
#if BOARD_CAN_POWER_OFF
    if (power_pending) power_off();
    else if (long_pending) long_press();
    else
#endif
    short_press();
  }
  if (!pressed) long_pending = power_pending = false;
  previous_pressed = pressed;
}

void receiver_begin(void) {
  serial_begin(115200); board_begin();
  radio_set_band_available(RADIO_BAND_AIR, BOARD_HAS_AIR); // Before restoring memories.
  radio_model_init(&model);
  load_settings();
  if (BOARD_HAS_RTC) restore_clock();
  lcd = board_display_begin();
  if (lcd) { buffered = ui_begin(theme); board_display_set_brightness(brightness); }
  supply = board_battery_read();
  const bool radio_ready = board_radio_begin();
  board_radio_set_powered(true); board_radio_reset();
  si473x_init(&chip, board_radio_bus());
  chip.log = log_line; chip.external_clock = BOARD_RADIO_EXTERNAL_CLOCK;
  failed = !si473x_begin(&chip);
  serial_printf("Receiver boot: board=%s LCD=%d framebuffer=%d largestblock=%u radiohw=%d I2C=0x%02x found=%d\n", BOARD_NAME, lcd, buffered,
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), radio_ready, chip.address, !failed);
  edited_at = millis() - 200;
  // A key still held from power-on must be released before it acts.
  previous_pressed = held = board_button_pressed();
  if (failed) fail();
}
void receiver_update(void) {
  const uint32_t now = millis();
  button_input(now);
  key_input(now);
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
    if (!failed && pending_seek && !chip.tuning && !chip.seeking && on_frequency()) {
      const radio_band_t* band = radio_model_band(&model);
      seek_direction = pending_seek; pending_seek = 0;
      audio_ready = false;
      if (set_audio_muted(true) && !si473x_seek(&chip, seek_direction > 0, band->min_hz, band->max_hz, radio_model_step(&model))) fail();
      dirty = true;
    }
    if (!failed && !chip.tuning && !chip.seeking && !on_frequency() && now - last_tune >= 30) {
      const uint32_t rf = radio_model_frequency(&model), receiver_hz = board_frontend_receiver_hz(rf);
      last_tune = now;
      if (frontend_hz != rf) {
        frontend_hz = 0;
        if (board_frontend_set(rf)) frontend_hz = rf;
        else { frontend_failed = true; fail(); }
      }
      if (!failed && chip.confirmed_hz != receiver_hz && !si473x_tune(&chip, receiver_hz)) fail();
      dirty = true;
    }
    if (!failed && !chip.tuning && !chip.seeking && !pending_seek && on_frequency() && !audio_ready) {
      if (set_audio_muted(muted)) { audio_ready = true; board_speaker_set_enabled(true); }
      dirty = true;
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
  if (rds_clock_sync_update(&rds_clock_sync, rds_clock_time(&chip.rds), &device_clock, hal_monotonic_ms()) && BOARD_HAS_RTC)
    store_clock();
  if (board_environment_poll(now, &environment)) { environment_known = true; dirty = true; }
  remember_settings(now);
  if (BOARD_AUDIO_RATE && now - last_spectrum >= 66) {
    uint16_t samples[SPECTRUM_SAMPLES];
    if (board_audio_read(samples, SPECTRUM_SAMPLES)) {
      spectrum_bars(samples, spectrum); spectrum_known = true; last_spectrum = now;
      if (lcd && !dirty) draw(true);
    }
  }
  if (dirty && now - last_draw >= 50) { dirty = false; last_draw = now; if (lcd) draw(false); }
  if (screenshot_requested && (last_draw || !lcd || !buffered)) {
    screenshot_requested = false;
    if (!lcd) serial_println("SCREENSHOT_ERROR framebuffer unavailable");
    else ui_capture();
  }
  delay(1);
}

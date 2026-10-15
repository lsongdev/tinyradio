#pragma once
#include <stdbool.h>
#include "drivers/si473x/si473x.h"
#include "hal/battery.h"
#include "hal/environment.h"
#include "radio/model.h"
// Item with encoder focus, in short-press cycling order: one loop around the
// screen, from the band row down through the controls and back along the
// status bar from right to left.
typedef enum {
  UI_FIELD_BAND, UI_FIELD_FREQUENCY, UI_FIELD_MODE, UI_FIELD_BANDWIDTH, UI_FIELD_GAIN,
  UI_FIELD_SOFT_MUTE, UI_FIELD_STEP, UI_FIELD_BATTERY, UI_FIELD_VOLUME, UI_FIELD_BRIGHTNESS, UI_FIELD_COUNT
} ui_field_t;
// Everything one frame shows. The application fills it; the UI only reads it.
typedef struct {
  const radio_model_t* model;
  ui_field_t field;
  int volume, brightness;
  bool muted;
  hal_battery_reading_t supply;
  bool battery_volts;           // Show the supply voltage instead of the percentage.
  const char* clock;            // Local "HH:MM", or NULL before the first synchronization.
  const char* date_time;        // Local "YYYY-MM-DD HH:MM:SS", or NULL.
  hal_environment_reading_t environment;
  bool environment_known;       // The board has produced at least one reading.
  const uint8_t* spectrum;      // SPECTRUM_BARS audio levels (0..255, 0-8 kHz), or NULL without audio sampling.
  si473x_signal_t signal;
  si473x_reception_t reception; // Settings of the current mode.
  const char* ps;               // Confirmed PS, or "".
  const char* message;          // Confirmed eRT, else RT, or "".
  const char* error;            // Shown only when failed.
  const char* notice;           // Transient prompt, or NULL.
  bool failed;
  bool pending;                 // Configuring, tuning, seeking, or not yet at the requested frequency.
  bool seeking;                 // The chip is searching.
  bool seek_mode;
  bool band_locked;             // Manual tuning stays within the current band.
  int seek_arrow;               // Requested or active seek direction; 0 hides the arrow.
  bool audio_status_known;      // Stereo/mono is meaningful.
} ui_view_t;
// The shared layout fills the top 320 x 170. Taller panels get a board-extras
// area below it (audio spectrum, date, time, environment).
// Call after board_display_begin(). Returns false when the frame buffer is
// unavailable; drawing then goes directly to the panel and capture is disabled.
bool ui_begin(void);
void ui_draw(const ui_view_t* view);
// Redraws only the extras area (nothing on panels without one), e.g. for a new spectrum.
void ui_draw_extras(const ui_view_t* view);
// Writes the last presented frame to serial as RGB888, not a new rendering.
void ui_capture(void);

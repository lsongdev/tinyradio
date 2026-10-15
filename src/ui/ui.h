#pragma once
#include <stdbool.h>
#include "drivers/si473x/si473x.h"
#include "hal/battery.h"
#include "radio/model.h"
// Item with encoder focus, in short-press cycling order.
typedef enum {
  UI_FIELD_BAND, UI_FIELD_FREQUENCY, UI_FIELD_MODE, UI_FIELD_BANDWIDTH, UI_FIELD_GAIN,
  UI_FIELD_SOFT_MUTE, UI_FIELD_STEP, UI_FIELD_VOLUME, UI_FIELD_BRIGHTNESS, UI_FIELD_COUNT
} ui_field_t;
// Everything one frame shows. The application fills it; the UI only reads it.
typedef struct {
  const radio_model_t* model;
  ui_field_t field;
  int volume, brightness;
  bool muted;
  hal_battery_reading_t supply;
  const char* clock;            // Local "HH:MM", or NULL before the first synchronization.
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
  int seek_arrow;               // Requested or active seek direction; 0 hides the arrow.
  bool audio_status_known;      // Stereo/mono is meaningful.
} ui_view_t;
// Call after board_display_begin(). Returns false when the frame buffer is
// unavailable; drawing then goes directly to the panel and capture is disabled.
bool ui_begin(void);
void ui_draw(const ui_view_t* view);
// Writes the last presented frame to serial as RGB888, not a new rendering.
void ui_capture(void);

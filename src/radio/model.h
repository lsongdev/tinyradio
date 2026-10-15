#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "drivers/si473x/tuning.h"

typedef struct {
  const char* name;
  uint32_t min_hz, max_hz, initial_hz;
  bool fm, available;
} radio_band_t;
#define RADIO_BAND_COUNT 5
extern const radio_band_t radio_bands[RADIO_BAND_COUNT];
static inline bool radio_mode_allowed(const radio_band_t* band, si473x_mode_t mode) {
  return band->available && (unsigned)mode <= SI473X_USB && (band->fm ? mode == SI473X_FM : mode != SI473X_FM);
}
uint32_t radio_step_option(size_t band, si473x_mode_t mode, int index);
uint8_t radio_step_option_count(size_t band, si473x_mode_t mode);
int radio_step_option_index(size_t band, si473x_mode_t mode, uint32_t step_hz);

// Per-band memory in portable units, as saved and restored.
typedef struct { uint32_t frequency_hz; si473x_mode_t mode; uint32_t step_hz; } radio_band_memory_t;
typedef struct { size_t band; radio_band_memory_t memories[RADIO_BAND_COUNT]; } radio_state_t;

// Current band plus each band's frequency, mode and step choice. Plain data:
// copy by assignment, e.g. to preview tuning without changing the original.
typedef struct { uint32_t hz; si473x_mode_t mode; uint8_t step; } radio_memory_t;
typedef struct {
  size_t band;
  radio_memory_t memories[RADIO_BAND_COUNT];
} radio_model_t;

void radio_model_init(radio_model_t* model);
radio_state_t radio_model_state(const radio_model_t* model);
// Validates every available band before committing; a failed restore changes nothing.
bool radio_model_restore(radio_model_t* model, const radio_state_t* state);
static inline const radio_band_t* radio_model_band(const radio_model_t* model) { return &radio_bands[model->band]; }
static inline si473x_mode_t radio_model_mode(const radio_model_t* model) { return model->memories[model->band].mode; }
static inline uint32_t radio_model_frequency(const radio_model_t* model) { return model->memories[model->band].hz; }
uint32_t radio_model_step(const radio_model_t* model);
bool radio_model_set_band(radio_model_t* model, size_t index);
bool radio_model_set_mode(radio_model_t* model, si473x_mode_t mode);
bool radio_model_set_frequency(radio_model_t* model, uint32_t hz);
// Crosses into adjacent bands at their endpoints; stops at the global endpoints.
bool radio_model_turn_frequency(radio_model_t* model, int turns);
void radio_model_turn_band(radio_model_t* model, int turns);
void radio_model_turn_mode(radio_model_t* model, int turns);
void radio_model_turn_step(radio_model_t* model, int turns);

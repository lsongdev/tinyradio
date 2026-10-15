#include "model.h"
#include <string.h>

radio_band_t radio_bands[RADIO_BAND_COUNT] = {
  {"LW", 153000, 279000, 198000, false, true, false, true},
  {"MW", 520000, 1710000, 999000, false, true, false, true},
  {"SW", 2300000, 26100000, 7100000, false, true, false, true},
  {"VHF", 64000000, 108000000, 100000000, true, false, false, true},
  {"AIR", 118000000, 137000000, 118000000, false, false, true, false},
};
void radio_set_band_available(size_t index, bool available) {
  if (index < RADIO_BAND_COUNT) radio_bands[index].available = available;
}
static const uint32_t fm_steps[] = {10000, 50000, 100000, 200000};
static const uint32_t am_steps[] = {1000, 5000, 9000, 10000};
static const uint32_t ssb_steps[] = {10, 50, 100, 500, 1000};
static const uint32_t sw_am_steps[] = {1000, 5000, 9000, 10000, 100000};
static const uint32_t sw_ssb_steps[] = {10, 50, 100, 500, 1000, 10000, 100000};
static const uint32_t air_steps[] = {1000, 5000, 25000}; // AM channels are 25 kHz; 8.33 kHz is not supported.
static int wrap(int value, int size) { return (value % size + size) % size; }
static int64_t magnitude(int turns) { return turns < 0 ? -(int64_t)turns : turns; }

uint32_t radio_step_option(size_t band, si473x_mode_t mode, int index) {
  if (band == RADIO_BAND_AIR) return index >= 0 && index < 3 ? air_steps[index] : 0;
  if (mode == SI473X_FM) return index >= 0 && index < 4 ? fm_steps[index] : 0;
  if (band == 2 && si473x_is_ssb(mode)) return index >= 0 && index < 7 ? sw_ssb_steps[index] : 0;
  if (band == 2) return index >= 0 && index < 5 ? sw_am_steps[index] : 0;
  if (si473x_is_ssb(mode)) return index >= 0 && index < 5 ? ssb_steps[index] : 0;
  return index >= 0 && index < 4 ? am_steps[index] : 0;
}
uint8_t radio_step_option_count(size_t band, si473x_mode_t mode) {
  if (band == RADIO_BAND_AIR) return 3;
  if (mode == SI473X_FM) return 4;
  if (band == 2 && si473x_is_ssb(mode)) return 7;
  if (band == 2) return 5;
  return si473x_is_ssb(mode) ? 5 : 4;
}
int radio_step_option_index(size_t band, si473x_mode_t mode, uint32_t step_hz) {
  for (int i = 0; i < radio_step_option_count(band, mode); ++i)
    if (radio_step_option(band, mode, i) == step_hz) return i;
  return -1;
}
static uint32_t step_for_band(const radio_model_t* model, size_t index) {
  return radio_step_option(index, model->memories[index].mode, model->memories[index].step);
}
void radio_model_init(radio_model_t* model) {
  memset(model, 0, sizeof *model);
  model->band = 3;
  for (size_t i = 0; i < RADIO_BAND_COUNT; ++i) {
    model->memories[i].hz = radio_bands[i].initial_hz;
    model->memories[i].mode = radio_bands[i].fm ? SI473X_FM : SI473X_AM;
    model->memories[i].step = radio_bands[i].fm || i == 1 || i == RADIO_BAND_AIR ? 2 : 0;
  }
}
radio_state_t radio_model_state(const radio_model_t* model) {
  radio_state_t result;
  memset(&result, 0, sizeof result);
  result.band = model->band;
  for (size_t i = 0; i < RADIO_BAND_COUNT; ++i) {
    result.memories[i].frequency_hz = model->memories[i].hz;
    result.memories[i].mode = model->memories[i].mode;
    result.memories[i].step_hz = step_for_band(model, i);
  }
  return result;
}
bool radio_model_restore(radio_model_t* model, const radio_state_t* state) {
  if (state->band >= RADIO_BAND_COUNT || !radio_bands[state->band].available) return false;
  radio_model_t candidate;
  radio_model_init(&candidate);
  for (size_t i = 0; i < RADIO_BAND_COUNT; ++i) {
    if (!radio_bands[i].available) continue;
    const radio_band_memory_t* memory = &state->memories[i];
    if (!radio_model_set_band(&candidate, i) || !radio_model_set_mode(&candidate, memory->mode) ||
        !radio_model_set_frequency(&candidate, memory->frequency_hz)) return false;
    const int step_index = radio_step_option_index(i, memory->mode, memory->step_hz);
    if (step_index < 0) return false;
    const int current_index = radio_step_option_index(i, memory->mode, radio_model_step(&candidate));
    if (current_index < 0) return false;
    radio_model_turn_step(&candidate, step_index - current_index);
  }
  radio_model_set_band(&candidate, state->band);
  *model = candidate;
  return true;
}
uint32_t radio_model_step(const radio_model_t* model) { return step_for_band(model, model->band); }
bool radio_model_set_band(radio_model_t* model, size_t index) {
  if (index >= RADIO_BAND_COUNT || !radio_bands[index].available) return false;
  model->band = index;
  return true;
}
bool radio_model_set_mode(radio_model_t* model, si473x_mode_t mode) {
  if ((unsigned)mode > SI473X_USB || !radio_mode_allowed(radio_model_band(model), mode)) return false;
  radio_memory_t* memory = &model->memories[model->band];
  if (memory->mode == mode) return true;
  memory->mode = mode;
  memory->step = si473x_is_ssb(mode) ? 2 : (radio_model_band(model)->fm || model->band == 1 ? 2 : 0);
  if (!si473x_is_ssb(mode)) memory->hz = memory->hz / 1000 * 1000;
  return true;
}
bool radio_model_set_frequency(radio_model_t* model, uint32_t hz) {
  const radio_band_t* band = radio_model_band(model);
  si473x_tune_word_t word;
  if (hz < band->min_hz || hz > band->max_hz) return false;
  // Converted bands reach the chip at a fixed IF; keep their RF on the AM kHz grid.
  if (band->converted ? hz % 1000 != 0 : !si473x_encode_frequency(radio_model_mode(model), hz, &word)) return false;
  model->memories[model->band].hz = hz;
  return true;
}
bool radio_model_turn_frequency(radio_model_t* model, int turns) {
  const int direction = turns < 0 ? -1 : 1;
  int64_t remaining = magnitude(turns);
  while (remaining) {
    const radio_band_t* band = radio_model_band(model);
    const uint32_t step = radio_model_step(model), hz = radio_model_frequency(model);
    const uint32_t distance = direction > 0 ? band->max_hz - hz : hz - band->min_hz;
    const int64_t room = distance / step;
    if (remaining <= room)
      return radio_model_set_frequency(model, (uint32_t)((int64_t)hz + direction * remaining * step));
    model->memories[model->band].hz = (uint32_t)((int64_t)hz + direction * room * step);
    remaining -= room + 1;
    int next = (int)model->band + direction;
    while (next >= 0 && next < RADIO_BAND_COUNT && !radio_bands[next].available) next += direction;
    if (next < 0 || next >= RADIO_BAND_COUNT)
      return radio_model_set_frequency(model, direction > 0 ? band->max_hz : band->min_hz);
    radio_model_set_band(model, next); // Cross adjacent bands, but never join the global endpoints.
    model->memories[model->band].hz = direction > 0 ? radio_bands[next].min_hz : radio_bands[next].max_hz;
  }
  return true;
}
bool radio_model_turn_frequency_in_band(radio_model_t* model, int turns) {
  const int direction = turns < 0 ? -1 : 1;
  const int64_t remaining = magnitude(turns);
  const radio_band_t* band = radio_model_band(model);
  const uint32_t step = radio_model_step(model), hz = radio_model_frequency(model);
  const uint32_t distance = direction > 0 ? band->max_hz - hz : hz - band->min_hz;
  if (remaining <= distance / step)
    return radio_model_set_frequency(model, (uint32_t)((int64_t)hz + direction * remaining * step));
  return radio_model_set_frequency(model, direction > 0 ? band->max_hz : band->min_hz);
}
void radio_model_turn_band(radio_model_t* model, int turns) {
  int available = 0;
  for (size_t i = 0; i < RADIO_BAND_COUNT; ++i) available += radio_bands[i].available;
  turns %= available; // Avoid unbounded work for accumulated input.
  int index = (int)model->band;
  const int direction = turns < 0 ? -1 : 1;
  for (int64_t i = 0; i < magnitude(turns); ++i) {
    do { index = wrap(index + direction, RADIO_BAND_COUNT); } while (!radio_bands[index].available);
  }
  radio_model_set_band(model, index);
}
void radio_model_turn_mode(radio_model_t* model, int turns) {
  const radio_band_t* band = radio_model_band(model);
  int allowed = 0;
  for (int i = SI473X_FM; i <= SI473X_USB; ++i) allowed += radio_mode_allowed(band, (si473x_mode_t)i);
  turns %= allowed;
  int mode = (int)radio_model_mode(model);
  const int direction = turns < 0 ? -1 : 1;
  for (int64_t i = 0; i < magnitude(turns); ++i) {
    do { mode = wrap(mode + direction, 4); } while (!radio_mode_allowed(band, (si473x_mode_t)mode));
  }
  radio_model_set_mode(model, (si473x_mode_t)mode);
}
void radio_model_turn_step(radio_model_t* model, int turns) {
  radio_memory_t* memory = &model->memories[model->band];
  memory->step = wrap(memory->step + turns, radio_step_option_count(model->band, memory->mode));
}

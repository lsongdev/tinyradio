#include "saved_settings.h"
#include <string.h>

static void put32(uint8_t* p, uint32_t value) {
  for (int i = 0; i < 4; ++i) p[i] = value >> (i * 8);
}
static uint32_t get32(const uint8_t* p) {
  uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value |= (uint32_t)p[i] << (i * 8);
  return value;
}
static uint32_t crc(const uint8_t* bytes) {
  uint32_t value = 0xffffffff;
  for (size_t i = 0; i < RADIO_SETTINGS_SIZE - 4; ++i) {
    value ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  }
  return ~value;
}
static bool valid(const radio_settings_t* settings) {
  radio_model_t model;
  radio_model_init(&model);
  if ((unsigned)settings->theme >= UI_THEME_COUNT || settings->volume > 63 || settings->brightness < 8 || !radio_model_restore(&model, &settings->tuning)) return false;
  for (int i = 0; i < 4; ++i) {
    const si473x_mode_t mode = (si473x_mode_t)i;
    const si473x_reception_t* r = &settings->reception[i];
    int count; si473x_bandwidths(mode, &count);
    if (r->bandwidth >= count || r->gain < -1 || r->gain > si473x_max_gain_index(mode) || r->soft_mute_db > 31) return false;
  }
  return true;
}
void radio_settings_default(radio_settings_t* settings) {
  static const si473x_reception_t reception[4] = {{0, -1, 16}, {4, -1, 0}, {3, -1, 0}, {3, -1, 0}};
  radio_model_t model;
  radio_model_init(&model);
  memset(settings, 0, sizeof *settings);
  settings->tuning = radio_model_state(&model);
  settings->volume = 18; settings->brightness = 128;
  memcpy(settings->reception, reception, sizeof reception);
}
bool radio_settings_encode(const radio_settings_t* settings, uint8_t bytes[RADIO_SETTINGS_SIZE]) {
  if (!valid(settings)) return false;
  memset(bytes, 0, RADIO_SETTINGS_SIZE);
  bytes[0] = 'A'; bytes[1] = 'T'; bytes[2] = 'S'; bytes[3] = 'M';
  bytes[4] = 3; bytes[5] = RADIO_BAND_COUNT; bytes[6] = 4; bytes[7] = (uint8_t)settings->tuning.band;
  bytes[8] = settings->volume; bytes[9] = settings->brightness; bytes[10] = settings->muted;
  bytes[11] = (uint8_t)settings->theme;
  for (size_t i = 0; i < RADIO_BAND_COUNT; ++i) {
    uint8_t* p = bytes + 16 + i * 12; const radio_band_memory_t* memory = &settings->tuning.memories[i];
    p[0] = (uint8_t)memory->mode; put32(p + 4, memory->frequency_hz); put32(p + 8, memory->step_hz);
  }
  for (int i = 0; i < 4; ++i) {
    uint8_t* p = bytes + 16 + RADIO_BAND_COUNT * 12 + i * 4; const si473x_reception_t* r = &settings->reception[i];
    p[0] = r->bandwidth; p[1] = (uint8_t)(r->gain + 1); p[2] = r->soft_mute_db;
  }
  put32(bytes + RADIO_SETTINGS_SIZE - 4, crc(bytes));
  return true;
}
bool radio_settings_decode(const uint8_t bytes[RADIO_SETTINGS_SIZE], radio_settings_t* settings) {
  if (bytes[0] != 'A' || bytes[1] != 'T' || bytes[2] != 'S' || bytes[3] != 'M' || (bytes[4] < 1 || bytes[4] > 3) ||
      bytes[5] != RADIO_BAND_COUNT || bytes[6] != 4 || bytes[10] > 1 ||
      (bytes[4] == 2 && bytes[11] > 1) ||
      (bytes[4] == 3 && bytes[11] >= UI_THEME_COUNT) ||
      get32(bytes + RADIO_SETTINGS_SIZE - 4) != crc(bytes)) return false;
  radio_settings_t candidate;
  radio_settings_default(&candidate);
  candidate.tuning.band = bytes[7]; candidate.volume = bytes[8]; candidate.brightness = bytes[9]; candidate.muted = bytes[10];
  candidate.theme = bytes[4] == 1 ? UI_THEME_DARK : (ui_theme_id_t)bytes[11];
  for (size_t i = 0; i < RADIO_BAND_COUNT; ++i) {
    const uint8_t* p = bytes + 16 + i * 12;
    radio_band_memory_t* memory = &candidate.tuning.memories[i];
    memory->frequency_hz = get32(p + 4); memory->mode = (si473x_mode_t)p[0]; memory->step_hz = get32(p + 8);
  }
  for (int i = 0; i < 4; ++i) {
    const uint8_t* p = bytes + 16 + RADIO_BAND_COUNT * 12 + i * 4;
    if (p[1] > 38) return false;
    candidate.reception[i].bandwidth = p[0];
    candidate.reception[i].gain = (int8_t)(p[1] - 1);
    candidate.reception[i].soft_mute_db = p[2];
  }
  if (!valid(&candidate)) return false;
  *settings = candidate;
  return true;
}

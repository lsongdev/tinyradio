#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "model.h"
#include "drivers/si473x/settings.h"
typedef struct {
  radio_state_t tuning;
  uint8_t volume, brightness;
  bool muted;
  si473x_reception_t reception[4]; // Indexed by si473x_mode_t.
} radio_settings_t;
// Explicit, versioned bytes; never persist C struct padding or enum layout.
#define RADIO_SETTINGS_SIZE (16 + RADIO_BAND_COUNT * 12 + 4 * 4 + 4)
void radio_settings_default(radio_settings_t* settings);
bool radio_settings_encode(const radio_settings_t* settings, uint8_t bytes[RADIO_SETTINGS_SIZE]);
// Leaves settings untouched unless every field is valid.
bool radio_settings_decode(const uint8_t bytes[RADIO_SETTINGS_SIZE], radio_settings_t* settings);

#include "radio/saved_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static si473x_reception_t reception(uint8_t bandwidth, int8_t gain, uint8_t soft_mute_db) {
  const si473x_reception_t result = {bandwidth, gain, soft_mute_db};
  return result;
}
static bool encodes(const radio_settings_t* settings) {
  uint8_t bytes[RADIO_SETTINGS_SIZE];
  return radio_settings_encode(settings, bytes);
}
int main(void) {
  radio_model_t source; radio_model_init(&source);
  radio_model_set_band(&source, 0); radio_model_set_frequency(&source, 162000); radio_model_turn_step(&source, 1);
  radio_model_set_band(&source, 1); radio_model_set_mode(&source, SI473X_LSB); radio_model_set_frequency(&source, 1000123); radio_model_turn_step(&source, -1);
  radio_model_set_band(&source, 3); radio_model_set_frequency(&source, 103700000); radio_model_turn_step(&source, 1);
  radio_model_set_band(&source, 2); radio_model_set_mode(&source, SI473X_USB); radio_model_set_frequency(&source, 7100123); radio_model_turn_step(&source, 2);
  radio_settings_t settings; radio_settings_default(&settings);
  settings.tuning = radio_model_state(&source);
  settings.volume = 37; settings.brightness = 208; settings.muted = true;
  settings.reception[0] = reception(4,26,31); settings.reception[1] = reception(6,37,1);
  settings.reception[2] = reception(0,-1,0); settings.reception[3] = reception(5,12,8);
  uint8_t encoded[RADIO_SETTINGS_SIZE], reencoded[RADIO_SETTINGS_SIZE];
  assert(radio_settings_encode(&settings, encoded));
  radio_settings_t loaded;
  assert(radio_settings_decode(encoded, &loaded));
  assert(radio_settings_encode(&loaded, reencoded) && !memcmp(encoded, reencoded, sizeof encoded));
  assert(loaded.volume == 37 && loaded.brightness == 208 && loaded.muted);
  radio_model_t restored; radio_model_init(&restored);
  assert(radio_model_restore(&restored, &loaded.tuning) && restored.band == 2);
  for (size_t i = 0; i < 4; ++i) {
    radio_model_set_band(&source, i); radio_model_set_band(&restored, i);
    assert(radio_model_frequency(&source) == radio_model_frequency(&restored) &&
           radio_model_mode(&source) == radio_model_mode(&restored) && radio_model_step(&source) == radio_model_step(&restored));
  }
  // Reject any damaged byte, including metadata/checksum; output stays intact.
  for (size_t i = 0; i < sizeof encoded; ++i) {
    uint8_t damaged[RADIO_SETTINGS_SIZE]; memcpy(damaged, encoded, sizeof damaged); damaged[i] ^= 1;
    assert(!radio_settings_decode(damaged, &loaded));
    assert(loaded.volume == 37 && loaded.tuning.band == 2);
  }
  radio_settings_t invalid = settings;
  invalid.tuning.band = 4; assert(!encodes(&invalid));
  invalid = settings; invalid.tuning.memories[0].mode = SI473X_FM; assert(!radio_model_restore(&restored, &invalid.tuning));
  assert(restored.band == 3 && radio_model_frequency(&restored) == 103700000); // Failed restore is atomic.
  invalid = settings; invalid.tuning.memories[0].frequency_hz = 162001; assert(!encodes(&invalid));
  invalid = settings; invalid.tuning.memories[2].step_hz = 123; assert(!encodes(&invalid));
  invalid = settings; invalid.volume = 64; assert(!encodes(&invalid));
  invalid = settings; invalid.brightness = 0; assert(!encodes(&invalid));
  invalid = settings; invalid.reception[0].gain = 27; assert(!encodes(&invalid));
  invalid = settings; invalid.reception[3].bandwidth = 6; assert(!encodes(&invalid));
  invalid = settings; invalid.reception[1].soft_mute_db = 32; assert(!encodes(&invalid));
  puts("Persistent settings round-trip, per-band memories, corruption and atomic validation passed");
  return 0;
}

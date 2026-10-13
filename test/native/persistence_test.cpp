#include "radio/saved_settings.h"
#include <assert.h>
#include <stdio.h>
using drivers::Mode;
int main() {
  radio::Model source;
  source.setBand(0); source.setFrequency(162000); source.turnStep(1);
  source.setBand(1); source.setMode(Mode::LSB); source.setFrequency(1000123); source.turnStep(-1);
  source.setBand(3); source.setFrequency(103700000); source.turnStep(1);
  source.setBand(2); source.setMode(Mode::USB); source.setFrequency(7100123); source.turnStep(2);
  radio::SavedSettings settings; settings.tuning = source.state();
  settings.volume = 37; settings.brightness = 208; settings.muted = true;
  settings.reception[0] = {4,26,31}; settings.reception[1] = {6,37,1};
  settings.reception[2] = {0,-1,0}; settings.reception[3] = {5,12,8};
  radio::SettingsBytes encoded{}, reencoded{};
  assert(radio::encodeSettings(settings, encoded));
  radio::SavedSettings loaded;
  assert(radio::decodeSettings(encoded, loaded));
  assert(radio::encodeSettings(loaded, reencoded) && encoded == reencoded);
  assert(loaded.volume == 37 && loaded.brightness == 208 && loaded.muted);
  radio::Model restored; assert(restored.restore(loaded.tuning) && restored.bandIndex() == 2);
  for (size_t i = 0; i < 4; ++i) {
    source.setBand(i); restored.setBand(i);
    assert(source.frequencyHz() == restored.frequencyHz() && source.mode() == restored.mode() && source.stepHz() == restored.stepHz());
  }
  // Reject any damaged byte, including metadata/checksum; output stays intact.
  for (size_t i = 0; i < encoded.size(); ++i) {
    auto damaged = encoded; damaged[i] ^= 1;
    assert(!radio::decodeSettings(damaged, loaded));
    assert(loaded.volume == 37 && loaded.tuning.band == 2);
  }
  auto invalid = settings;
  invalid.tuning.band = 4; assert(!radio::encodeSettings(invalid, reencoded));
  invalid = settings; invalid.tuning.memories[0].mode = Mode::FM; assert(!restored.restore(invalid.tuning));
  assert(restored.bandIndex() == 3 && restored.frequencyHz() == 103700000); // Failed restore is atomic.
  invalid = settings; invalid.tuning.memories[0].frequencyHz = 162001; assert(!radio::encodeSettings(invalid, reencoded));
  invalid = settings; invalid.tuning.memories[2].stepHz = 123; assert(!radio::encodeSettings(invalid, reencoded));
  invalid = settings; invalid.volume = 64; assert(!radio::encodeSettings(invalid, reencoded));
  invalid = settings; invalid.brightness = 0; assert(!radio::encodeSettings(invalid, reencoded));
  invalid = settings; invalid.reception[0].gain = 27; assert(!radio::encodeSettings(invalid, reencoded));
  invalid = settings; invalid.reception[3].bandwidth = 6; assert(!radio::encodeSettings(invalid, reencoded));
  invalid = settings; invalid.reception[1].softMuteDb = 32; assert(!radio::encodeSettings(invalid, reencoded));
  puts("Persistent settings round-trip, per-band memories, corruption and atomic validation passed");
}

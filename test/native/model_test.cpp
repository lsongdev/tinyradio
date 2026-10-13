#include "radio/model.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
using drivers::Mode;
void valid(const radio::Model& model) {
  assert(model.band().available);
  assert(model.frequencyHz() >= model.band().minHz && model.frequencyHz() <= model.band().maxHz);
  assert(radio::allowed(model.band(), model.mode()));
}
int main() {
  radio::Model model;
  assert(!model.setBand(4) && !model.setBand(99));
  assert(!model.setMode(Mode::AM) && !model.setMode(Mode::USB));
  // Adjacent boundaries cross; global endpoints stop.
  for (int band = 0; band < 4; ++band) {
    model.setBand(band); model.setFrequency(model.band().maxHz);
    model.turnFrequency(1);
    assert(model.bandIndex() == size_t(band == 3 ? 3 : band + 1));
    assert(model.frequencyHz() == (band == 3 ? model.band().maxHz : model.band().minHz)); valid(model);
    model.setBand(band); model.setFrequency(model.band().minHz);
    model.turnFrequency(-1);
    assert(model.bandIndex() == size_t(band == 0 ? 0 : band - 1));
    assert(model.frequencyHz() == (band == 0 ? model.band().minHz : model.band().maxHz)); valid(model);
  }
  model.setBand(2); model.setMode(Mode::USB); model.setFrequency(26100000);
  model.turnFrequency(3);
  assert(model.bandIndex() == 3 && model.mode() == Mode::FM && model.stepHz() == 100000);
  assert(model.frequencyHz() == 64200000);
  model.setFrequency(64000000); model.turnFrequency(-3);
  assert(model.bandIndex() == 2 && model.mode() == Mode::USB && model.stepHz() == 100);
  assert(model.frequencyHz() == 26099800);
  // Batched counts must match detent-by-detent traversal, including memories.
  const int counts[] = {25000, -25000, 250000, -250000};
  for (int count : counts) {
    radio::Model batch, singles;
    batch.turnFrequency(count);
    for (int i = 0; i < (count < 0 ? -count : count); ++i) singles.turnFrequency(count < 0 ? -1 : 1);
    assert(batch.bandIndex() == singles.bandIndex());
    for (int band = 0; band < 4; ++band) {
      batch.setBand(band); singles.setBand(band);
      assert(batch.frequencyHz() == singles.frequencyHz());
    }
  }
  model.turnFrequency(INT_MIN); valid(model);
  assert(model.bandIndex() == 0 && model.frequencyHz() == 153000);
  model.turnFrequency(INT_MAX); valid(model);
  assert(model.bandIndex() == 3 && model.frequencyHz() == 108000000);
  model.setBand(2); model.setMode(Mode::USB); model.setFrequency(7100900); model.turnFrequency(2);
  assert(model.frequencyHz() == 7101100);
  drivers::TuneWord word;
  assert(drivers::encodeFrequency(Mode::USB, model.frequencyHz(), word));
  assert(word.carrier == 7101 && word.bfo == -100);
  model.setMode(Mode::AM); assert(model.frequencyHz() == 7101000);
  model.setBand(3); assert(!model.setFrequency(100000001));
  model.setBand(2); assert(model.frequencyHz() == 7101000);
  for (const auto& band : radio::bands)
    for (int mode = 0; mode < 4; ++mode)
      assert(radio::allowed(band, static_cast<Mode>(mode)) == (band.available && (band.fm ? mode == 0 : mode != 0)));
  assert(!drivers::encodeFrequency(Mode::FM, 118000000, word));
  assert(!drivers::encodeFrequency(Mode::AM, 7100100, word));
  assert(!drivers::encodeFrequency(Mode::USB, 30000001, word));
  puts("Radio model: adjacent-band boundaries, mode/step restoration, batched counts, limits and BFO passed");
}

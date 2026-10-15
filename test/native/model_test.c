#include "radio/model.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static const radio_band_t* band_of(const radio_model_t* model) { return radio_model_band(model); }
static void valid(const radio_model_t* model) {
  assert(band_of(model)->available);
  assert(radio_model_frequency(model) >= band_of(model)->min_hz && radio_model_frequency(model) <= band_of(model)->max_hz);
  assert(radio_mode_allowed(band_of(model), radio_model_mode(model)));
}
int main(void) {
  radio_model_t model; radio_model_init(&model);
  assert(!radio_model_set_band(&model, 4) && !radio_model_set_band(&model, 99));
  assert(!radio_model_set_mode(&model, SI473X_AM) && !radio_model_set_mode(&model, SI473X_USB));
  // Adjacent boundaries cross; global endpoints stop.
  for (int band = 0; band < 4; ++band) {
    radio_model_set_band(&model, band); radio_model_set_frequency(&model, band_of(&model)->max_hz);
    radio_model_turn_frequency(&model, 1);
    assert(model.band == (size_t)(band == 3 ? 3 : band + 1));
    assert(radio_model_frequency(&model) == (band == 3 ? band_of(&model)->max_hz : band_of(&model)->min_hz)); valid(&model);
    radio_model_set_band(&model, band); radio_model_set_frequency(&model, band_of(&model)->min_hz);
    radio_model_turn_frequency(&model, -1);
    assert(model.band == (size_t)(band == 0 ? 0 : band - 1));
    assert(radio_model_frequency(&model) == (band == 0 ? band_of(&model)->min_hz : band_of(&model)->max_hz)); valid(&model);
  }
  radio_model_set_band(&model, 2); radio_model_set_mode(&model, SI473X_USB); radio_model_set_frequency(&model, 26100000);
  radio_model_turn_frequency(&model, 3);
  assert(model.band == 3 && radio_model_mode(&model) == SI473X_FM && radio_model_step(&model) == 100000);
  assert(radio_model_frequency(&model) == 64200000);
  radio_model_set_frequency(&model, 64000000); radio_model_turn_frequency(&model, -3);
  assert(model.band == 2 && radio_model_mode(&model) == SI473X_USB && radio_model_step(&model) == 100);
  assert(radio_model_frequency(&model) == 26099800);
  // Batched counts must match detent-by-detent traversal, including memories.
  const int counts[] = {25000, -25000, 250000, -250000};
  for (int c = 0; c < 4; ++c) {
    const int count = counts[c];
    radio_model_t batch, singles; radio_model_init(&batch); radio_model_init(&singles);
    radio_model_turn_frequency(&batch, count);
    for (int i = 0; i < (count < 0 ? -count : count); ++i) radio_model_turn_frequency(&singles, count < 0 ? -1 : 1);
    assert(batch.band == singles.band);
    for (int band = 0; band < 4; ++band) {
      radio_model_set_band(&batch, band); radio_model_set_band(&singles, band);
      assert(radio_model_frequency(&batch) == radio_model_frequency(&singles));
    }
  }
  radio_model_turn_frequency(&model, INT_MIN); valid(&model);
  assert(model.band == 0 && radio_model_frequency(&model) == 153000);
  radio_model_turn_frequency(&model, INT_MAX); valid(&model);
  assert(model.band == 3 && radio_model_frequency(&model) == 108000000);
  radio_model_set_band(&model, 2); radio_model_set_mode(&model, SI473X_USB);
  radio_model_set_frequency(&model, 7100900); radio_model_turn_frequency(&model, 2);
  assert(radio_model_frequency(&model) == 7101100);
  si473x_tune_word_t word;
  assert(si473x_encode_frequency(SI473X_USB, radio_model_frequency(&model), &word));
  assert(word.carrier == 7101 && word.bfo == -100);
  radio_model_set_mode(&model, SI473X_AM); assert(radio_model_frequency(&model) == 7101000);
  radio_model_set_band(&model, 3); assert(!radio_model_set_frequency(&model, 100000001));
  radio_model_set_band(&model, 2); assert(radio_model_frequency(&model) == 7101000);
  for (int band = 0; band < RADIO_BAND_COUNT; ++band)
    for (int mode = 0; mode < 4; ++mode)
      assert(radio_mode_allowed(&radio_bands[band], (si473x_mode_t)mode) ==
             (radio_bands[band].available && (radio_bands[band].fm ? mode == 0 : mode != 0)));
  assert(!si473x_encode_frequency(SI473X_FM, 118000000, &word));
  assert(!si473x_encode_frequency(SI473X_AM, 7100100, &word));
  assert(!si473x_encode_frequency(SI473X_USB, 30000001, &word));
  puts("Radio model: adjacent-band boundaries, mode/step restoration, batched counts, limits and BFO passed");
  return 0;
}

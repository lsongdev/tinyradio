#include "services/spectrum.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void tone(uint16_t samples[SPECTRUM_SAMPLES], float hz, float amplitude) {
  for (int i = 0; i < SPECTRUM_SAMPLES; ++i)
    samples[i] = (uint16_t)lroundf(2048 + amplitude * sinf(6.28318530718f * hz * i / 16000));
}
static int loudest(const uint8_t bars[SPECTRUM_BARS]) {
  int best = 0;
  for (int b = 1; b < SPECTRUM_BARS; ++b) if (bars[b] > bars[best]) best = b;
  return best;
}
int main(void) {
  uint16_t samples[SPECTRUM_SAMPLES];
  uint8_t bars[SPECTRUM_BARS];
  // Silence, or a constant offset, has no spectrum.
  for (int i = 0; i < SPECTRUM_SAMPLES; ++i) samples[i] = 3000;
  spectrum_bars(samples, bars);
  for (int b = 0; b < SPECTRUM_BARS; ++b) assert(bars[b] == 0);
  // A tone lands in its bar (250 Hz per bar at 16 kHz).
  const float tones[] = {1000, 3125, 7000};
  for (int t = 0; t < 3; ++t) {
    tone(samples, tones[t], 1500);
    spectrum_bars(samples, bars);
    assert(loudest(bars) == (int)(tones[t] / 250));
  }
  // Near full scale reads near the top; 40 dB lower reads about half as high.
  tone(samples, 1000, 2000); spectrum_bars(samples, bars);
  const int loud = bars[4];
  assert(loud > 240);
  tone(samples, 1000, 20); spectrum_bars(samples, bars);
  assert(bars[4] > loud - 140 && bars[4] < loud - 115);
  puts("Audio spectrum: silence, tone placement and dB scale passed");
  return 0;
}

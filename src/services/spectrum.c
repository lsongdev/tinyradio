#include "spectrum.h"
#include <math.h>

void spectrum_bars(const uint16_t samples[SPECTRUM_SAMPLES], uint8_t bars[SPECTRUM_BARS]) {
  enum { N = SPECTRUM_SAMPLES };
  static const float tau = 6.28318530718f;
  float re[N], im[N], mean = 0;
  for (int i = 0; i < N; ++i) mean += samples[i];
  mean /= N;
  // Remove DC, then apply a Hann window.
  for (int i = 0; i < N; ++i) {
    re[i] = (samples[i] - mean) * 0.5f * (1 - cosf(tau * i / (N - 1)));
    im[i] = 0;
  }
  // In-place radix-2 FFT: bit-reversal permutation, then butterflies.
  for (int i = 1, j = 0; i < N; ++i) {
    int bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { const float t = re[i]; re[i] = re[j]; re[j] = t; }
  }
  for (int length = 2; length <= N; length <<= 1) {
    const float angle = -tau / length, wr = cosf(angle), wi = sinf(angle);
    for (int i = 0; i < N; i += length) {
      float ar = 1, ai = 0;
      for (int j = 0; j < length / 2; ++j) {
        const int p = i + j, q = p + length / 2;
        const float tr = ar * re[q] - ai * im[q], ti = ar * im[q] + ai * re[q];
        re[q] = re[p] - tr; im[q] = im[p] - ti; re[p] += tr; im[p] += ti;
        const float next = ar * wr - ai * wi; ai = ar * wi + ai * wr; ar = next;
      }
    }
  }
  for (int b = 0; b < SPECTRUM_BARS; ++b) {
    float peak = 0;
    for (int k = b * 4; k < (b + 1) * 4; ++k) {
      if (!k) continue; // DC.
      const float power = re[k] * re[k] + im[k] * im[k];
      if (power > peak) peak = power;
    }
    // Hann gain 0.5 and the single-sided spectrum give amplitude = 4|X|/N.
    const float amplitude = sqrtf(peak) * 4 / N;
    const float db = 20 * log10f(amplitude / 2048 + 0.000001f);
    const float level = (db + 80) * 255 / 80;
    bars[b] = (uint8_t)(level < 0 ? 0 : level > 255 ? 255 : level);
  }
}

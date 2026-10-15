#pragma once
#include <stdint.h>
// Audio spectrum of one block of unsigned 12-bit ADC samples: a Hann-windowed
// 256-point FFT folded into 32 bars, each the peak of four bins, as 0..255
// over -80..0 dB relative to full scale. At 16 kHz the bars cover 0-8 kHz.
enum { SPECTRUM_SAMPLES = 256, SPECTRUM_BARS = 32 };
void spectrum_bars(const uint16_t samples[SPECTRUM_SAMPLES], uint8_t bars[SPECTRUM_BARS]);

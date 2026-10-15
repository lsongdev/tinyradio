#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { SI473X_FM, SI473X_AM, SI473X_LSB, SI473X_USB } si473x_mode_t;
static inline bool si473x_is_ssb(si473x_mode_t mode) { return mode == SI473X_LSB || mode == SI473X_USB; }
static inline const char* si473x_mode_name(si473x_mode_t mode) {
  static const char* const names[] = {"FM", "AM", "LSB", "USB"};
  return names[mode];
}
typedef struct { uint16_t carrier; int16_t bfo; } si473x_tune_word_t;
static inline bool si473x_encode_frequency(si473x_mode_t mode, uint32_t hz, si473x_tune_word_t* word) {
  if (mode == SI473X_FM) {
    if (hz < 64000000 || hz > 108000000 || hz % 10000) return false;
    word->carrier = (uint16_t)(hz / 10000); word->bfo = 0;
  } else {
    if (hz < 150000 || hz > 30000000 || (!si473x_is_ssb(mode) && hz % 1000)) return false;
    // Increasing desired RF frequency requires a negative BFO offset.
    word->carrier = (uint16_t)(hz / 1000); word->bfo = (int16_t)-(int)(hz % 1000);
  }
  return true;
}

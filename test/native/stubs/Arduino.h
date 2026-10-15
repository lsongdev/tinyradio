#pragma once
// Host stand-ins for the Arduino C functions used by the C drivers.
#include <stdint.h>
static inline uint32_t millis(void) { static uint32_t now = 0; return ++now; }
static inline void delay(uint32_t ms) { (void)ms; }
static inline void delayMicroseconds(uint32_t us) { (void)us; }
#ifdef __cplusplus
template <typename T> static inline T constrain(T value, T low, T high) {
  return value < low ? low : value > high ? high : value;
}
#endif

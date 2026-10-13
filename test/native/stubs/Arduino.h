#pragma once
#include <stdint.h>
#include <algorithm>
#include <initializer_list>
#define PROGMEM
using byte = uint8_t;
inline uint8_t pgm_read_byte(const uint8_t* p) { return *p; }
inline uint16_t pgm_read_word(const uint16_t* p) { return *p; }
inline uint32_t millis() { static uint32_t now = 0; return ++now; }
inline void delay(unsigned) {}
inline void delayMicroseconds(unsigned) {}
template<class T> T constrain(T value, T low, T high) { return std::max(low, std::min(value, high)); }
struct TestSerial {
  template<class... T> void printf(const char*, T...) {}
  void print(const char*) {}
  void println() {}
};
static TestSerial Serial;

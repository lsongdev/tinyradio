#pragma once
#include <stdint.h>
namespace drivers {
enum class Mode : uint8_t { FM, AM, LSB, USB };
inline bool isSsb(Mode mode) { return mode == Mode::LSB || mode == Mode::USB; }
inline const char* modeName(Mode mode) {
  constexpr const char* names[] = {"FM", "AM", "LSB", "USB"};
  return names[static_cast<uint8_t>(mode)];
}
struct TuneWord { uint16_t carrier; int16_t bfo; };
inline bool encodeFrequency(Mode mode, uint32_t hz, TuneWord& word) {
  if (mode == Mode::FM) {
    if (hz < 64000000 || hz > 108000000 || hz % 10000) return false;
    word = {static_cast<uint16_t>(hz / 10000), 0};
  } else {
    if (hz < 150000 || hz > 30000000 || (!isSsb(mode) && hz % 1000)) return false;
    // Increasing desired RF frequency requires a negative BFO offset.
    word = {static_cast<uint16_t>(hz / 1000), static_cast<int16_t>(-static_cast<int>(hz % 1000))};
  }
  return true;
}
}

#pragma once
#include "drivers/si473x/tuning.h"
#include <stddef.h>
namespace radio {
using drivers::Mode;
struct Band { const char* name; uint32_t minHz, maxHz, initialHz; bool fm, available; };
constexpr Band bands[] = {
  {"LW", 153000, 279000, 198000, false, true},
  {"MW", 520000, 1710000, 999000, false, true},
  {"SW", 2300000, 26100000, 7100000, false, true},
  {"VHF", 64000000, 108000000, 100000000, true, true},
  {"AIR", 118000000, 137000000, 118000000, false, false},
};
constexpr size_t bandCount = sizeof bands / sizeof bands[0];
inline bool allowed(const Band& band, Mode mode) {
  return band.available && static_cast<uint8_t>(mode) <= 3 && (band.fm ? mode == Mode::FM : mode != Mode::FM);
}
class Model {
public:
  struct BandMemory { uint32_t frequencyHz; Mode mode; uint32_t stepHz; };
  struct State { size_t band; BandMemory memories[bandCount]; };
  Model();
  State state() const;
  bool restore(const State& state); // Validate all available bands before committing.
  const Band& band() const { return bands[bandIndex_]; }
  size_t bandIndex() const { return bandIndex_; }
  Mode mode() const { return memories_[bandIndex_].mode; }
  uint32_t frequencyHz() const { return memories_[bandIndex_].hz; }
  uint32_t stepHz() const;
  bool setBand(size_t index);
  bool setMode(Mode mode);
  bool setFrequency(uint32_t hz);
  bool turnFrequency(int turns);
  void turnBand(int turns);
  void turnMode(int turns);
  void turnStep(int turns);
private:
  uint32_t stepForBand(size_t index) const;
  struct Memory { uint32_t hz; Mode mode; uint8_t step; };
  Memory memories_[bandCount]{};
  size_t bandIndex_ = 3;
};
}

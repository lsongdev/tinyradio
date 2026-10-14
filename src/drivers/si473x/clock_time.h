#pragma once
#include <stdint.h>
namespace drivers {
// One complete 4A group supplies UTC MJD/minute and a signed half-hour offset.
// Sequence counts accepted updates/withdrawals; it is not a wall-clock value.
struct RdsClockTime {
  bool valid = false;
  uint32_t mjd = 0, sequence = 0;
  uint16_t pi = 0, utcMinutes = 0;
  int16_t offsetMinutes = 0;
};
inline void decodeClockTime(uint16_t pi, uint16_t b, uint16_t c, uint16_t d, RdsClockTime& clock) {
  const uint32_t mjd = (uint32_t(b & 3) << 15) | (c >> 1);
  const unsigned hour = ((c & 1) << 4) | (d >> 12), minute = (d >> 6) & 63;
  if (!mjd && !c && !d) { // All-zero CT codes withdraw an inaccurate clock.
    const uint32_t sequence = clock.sequence + 1;
    clock = {}; clock.sequence = sequence; return;
  }
  if (!mjd || mjd > 99999 || hour > 23 || minute > 59) return;
  const int offset = (d & 31) * ((d & 32) ? -30 : 30);
  clock.valid = true; clock.pi = pi; clock.mjd = mjd; clock.utcMinutes = hour * 60 + minute;
  clock.offsetMinutes = offset; ++clock.sequence;
}
}

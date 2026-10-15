#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
// One complete 4A group supplies UTC MJD/minute and a signed half-hour offset.
// Sequence counts accepted updates/withdrawals; it is not a wall-clock value.
typedef struct {
  bool valid;
  uint32_t mjd, sequence;
  uint16_t pi, utc_minutes;
  int16_t offset_minutes;
} rds_clock_time_t;
static inline void rds_decode_clock_time(uint16_t pi, uint16_t b, uint16_t c, uint16_t d, rds_clock_time_t* clock) {
  const uint32_t mjd = ((uint32_t)(b & 3) << 15) | (c >> 1);
  const unsigned hour = ((c & 1) << 4) | (d >> 12), minute = (d >> 6) & 63;
  if (!mjd && !c && !d) { // All-zero CT codes withdraw an inaccurate clock.
    const uint32_t sequence = clock->sequence + 1;
    memset(clock, 0, sizeof *clock); clock->sequence = sequence; return;
  }
  if (!mjd || mjd > 99999 || hour > 23 || minute > 59) return;
  clock->valid = true; clock->pi = pi; clock->mjd = mjd; clock->utc_minutes = (uint16_t)(hour * 60 + minute);
  clock->offset_minutes = (int16_t)((d & 31) * ((d & 32) ? -30 : 30)); ++clock->sequence;
}

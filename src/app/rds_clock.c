#include "rds_clock.h"
bool rds_clock_sync_update(rds_clock_sync_t* sync, const rds_clock_time_t* sample, device_clock_t* clock, uint64_t now_ms) {
  if (!sample->valid) return false; // Reception loss/withdrawal does not unset device time.
  const bool new_time = !sync->seen || sample->mjd != sync->mjd || sample->utc_minutes != sync->utc_minutes;
  if (new_time) {
    const int64_t utc_seconds = ((int64_t)sample->mjd - 40587) * 86400 + (int64_t)sample->utc_minutes * 60;
    device_clock_synchronize(clock, utc_seconds, TIME_SOURCE_RDS, now_ms);
  }
  const bool new_offset = !sync->seen || sample->offset_minutes != sync->offset_minutes;
  if (new_time || new_offset) device_clock_set_utc_offset(clock, sample->offset_minutes);
  // Deduplicate by broadcast content, including across tuning and PI changes.
  // Re-reading a minute or only changing its offset must not reset seconds.
  sync->seen = true; sync->mjd = sample->mjd;
  sync->utc_minutes = sample->utc_minutes; sync->offset_minutes = sample->offset_minutes;
  return new_time || new_offset;
}

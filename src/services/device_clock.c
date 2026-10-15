#include "device_clock.h"
#include <string.h>
const char* time_source_name(time_source_t source) {
  return source == TIME_SOURCE_RDS ? "RDS" : source == TIME_SOURCE_NTP ? "NTP" : "NONE";
}
void device_clock_synchronize(device_clock_t* clock, int64_t utc_seconds, time_source_t source, uint64_t now_ms) {
  if (source == TIME_SOURCE_NONE) return;
  clock->utc_seconds = utc_seconds; clock->synchronized_at = now_ms; clock->source = source;
}
void device_clock_set_utc_offset(device_clock_t* clock, int16_t minutes) { clock->utc_offset_minutes = minutes; }
device_time_t device_clock_read(const device_clock_t* clock, uint64_t now_ms) {
  device_time_t result;
  memset(&result, 0, sizeof result);
  if (clock->source == TIME_SOURCE_NONE || now_ms < clock->synchronized_at) return result;
  result.valid = true; result.source = clock->source; result.utc_offset_minutes = clock->utc_offset_minutes;
  result.utc_seconds = clock->utc_seconds + (int64_t)((now_ms - clock->synchronized_at) / 1000);
  result.local_seconds = result.utc_seconds + (int64_t)clock->utc_offset_minutes * 60;
  int64_t day_seconds = result.local_seconds % 86400;
  if (day_seconds < 0) day_seconds += 86400;
  result.hour = day_seconds / 3600; result.minute = day_seconds / 60 % 60; result.second = day_seconds % 60;
  return result;
}

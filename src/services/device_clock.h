#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { TIME_SOURCE_NONE, TIME_SOURCE_RDS, TIME_SOURCE_NTP } time_source_t;
const char* time_source_name(time_source_t source);
typedef struct {
  bool valid;
  int64_t utc_seconds, local_seconds;
  int16_t utc_offset_minutes;
  uint8_t hour, minute, second;
  time_source_t source;
} device_time_t;
// A shared device clock. Sources submit Unix UTC seconds; timezone is separate.
// now_ms is 64-bit monotonic time since boot, independent of radio state.
// Zero-initialized means not yet synchronized.
typedef struct {
  int64_t utc_seconds;
  uint64_t synchronized_at;
  int16_t utc_offset_minutes;
  time_source_t source;
} device_clock_t;
void device_clock_synchronize(device_clock_t* clock, int64_t utc_seconds, time_source_t source, uint64_t now_ms);
void device_clock_set_utc_offset(device_clock_t* clock, int16_t minutes);
device_time_t device_clock_read(const device_clock_t* clock, uint64_t now_ms);

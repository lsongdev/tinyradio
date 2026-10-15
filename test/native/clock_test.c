#include "drivers/si473x/rds.h"
#include "app/rds_clock.h"
#include "services/device_clock.h"
#include <assert.h>
#include <stdio.h>

static void ct_from(rds_decoder_t* r, uint32_t mjd, unsigned hour, unsigned minute, int half_hours,
                    uint8_t errors, bool version_b, uint16_t pi) {
  const uint16_t b = 0x4000 | (version_b ? 0x800 : 0) | (mjd >> 15);
  const uint16_t c = (uint16_t)(((mjd & 32767) << 1) | (hour >> 4));
  const unsigned offset = half_hours < 0 ? (unsigned)(32 | -half_hours) : (unsigned)half_hours;
  rds_decoder_accept(r, pi, b, (uint16_t)((hour << 12) | (minute << 6) | offset), errors, c);
}
static void ct(rds_decoder_t* r, uint32_t mjd, unsigned hour, unsigned minute, int half_hours) {
  ct_from(r, mjd, hour, minute, half_hours, 0, false, 0x1234);
}
int main(void) {
  rds_decoder_t decoder, *r = &decoder; rds_decoder_clear(r);
  device_clock_t clock = {0}; rds_clock_sync_t sync = {0};
  assert(!rds_clock_time(r)->valid && !device_clock_read(&clock, 0).valid);
  ct_from(r, 61327, 16, 45, 16, 0xaa, false, 0x1234); // Corrected blocks; C carries hour bit 4.
  const rds_clock_time_t t = *rds_clock_time(r);
  assert(t.valid && t.mjd == 61327 && t.utc_minutes == 1005 && t.offset_minutes == 480);
  rds_clock_sync_update(&sync, &t, &clock, 1000);
  device_time_t local = device_clock_read(&clock, 1000);
  const int64_t utc = ((int64_t)61327 - 40587) * 86400 + 1005 * 60;
  assert(local.valid && local.utc_seconds == utc && local.utc_offset_minutes == 480);
  assert(local.local_seconds / 86400 + 40587 == 61328 && local.hour == 0 && local.minute == 45);
  assert(device_clock_read(&clock, 61000).minute == 46);
  assert(device_clock_read(&clock, 601000).valid && device_clock_read(&clock, 601000).minute == 55); // No expiry.
  // Re-reading an old sample, even with a new sequence or PI, cannot rewind.
  rds_clock_sync_update(&sync, &t, &clock, 601000);
  ct(r, 61327, 16, 45, 16); rds_clock_sync_update(&sync, rds_clock_time(r), &clock, 601000);
  assert(device_clock_read(&clock, 601000).utc_seconds == utc + 600);
  rds_decoder_clear(r); rds_clock_sync_update(&sync, rds_clock_time(r), &clock, 602000);
  assert(device_clock_read(&clock, 602000).valid); // Tuning/seek invalidates CT, not device time.
  ct_from(r, 61327, 16, 45, 16, 0, false, 0x4321); rds_clock_sync_update(&sync, rds_clock_time(r), &clock, 603000);
  assert(device_clock_read(&clock, 603000).utc_seconds == utc + 602);
  ct_from(r, 0, 0, 0, 0, 0, false, 0x4321); rds_clock_sync_update(&sync, rds_clock_time(r), &clock, 604000);
  assert(!rds_clock_time(r)->valid && device_clock_read(&clock, 604000).utc_seconds == utc + 603);
  ct(r, 61327, 16, 56, 16); rds_clock_sync_update(&sync, rds_clock_time(r), &clock, 605000);
  assert(device_clock_read(&clock, 605000).hour == 0 && device_clock_read(&clock, 605000).minute == 56);
  // An offset-only change preserves the running seconds.
  ct(r, 61327, 16, 56, -1); rds_clock_sync_update(&sync, rds_clock_time(r), &clock, 615000);
  local = device_clock_read(&clock, 615000);
  assert(local.hour == 16 && local.minute == 26 && local.second == 10);
  const uint8_t errors[] = {0xc0, 0x30, 0x0c, 3};
  for (size_t i = 0; i < sizeof errors; ++i) {
    const uint32_t sequence = rds_clock_time(r)->sequence;
    ct_from(r, 61327, 18, 12, 0, errors[i], false, 0x1234); assert(rds_clock_time(r)->sequence == sequence);
  }
  const uint32_t sequence = rds_clock_time(r)->sequence;
  ct(r, 61327, 24, 0, 0); ct(r, 61327, 23, 60, 0);
  ct(r, 100000, 0, 0, 0); ct(r, 0, 1, 0, 0); ct_from(r, 61327, 12, 0, 0, 0, true, 0x1234);
  assert(rds_clock_time(r)->sequence == sequence);
  const int offsets[] = {-31, 0, 31};
  for (int i = 0; i < 3; ++i) {
    ct(r, 65536, 12, 59, offsets[i]);
    assert(rds_clock_time(r)->mjd == 65536 && rds_clock_time(r)->offset_minutes == offsets[i] * 30);
  }
  ct(r, 61327, 0, 15, -1); rds_clock_sync_update(&sync, rds_clock_time(r), &clock, 700000);
  local = device_clock_read(&clock, 700000);
  assert(local.local_seconds / 86400 + 40587 == 61326 && local.hour == 23 && local.minute == 45);
  ct(r, 99999, 23, 59, 31); rds_clock_sync_update(&sync, rds_clock_time(r), &clock, 800000);
  local = device_clock_read(&clock, 800000);
  assert(local.local_seconds / 86400 + 40587 == 100000 && local.hour == 15 && local.minute == 29);
  // Future NTP uses the identical UTC interface and UI reading, retaining timezone.
  const int64_t ntp_utc = 4102444800LL; // Beyond 2038; no 32-bit epoch truncation.
  device_clock_synchronize(&clock, ntp_utc, TIME_SOURCE_NTP, 900000);
  local = device_clock_read(&clock, 901234);
  assert(local.valid && local.source == TIME_SOURCE_NTP && local.utc_seconds == ntp_utc + 1);
  assert(local.utc_offset_minutes == 930 && local.local_seconds == ntp_utc + 1 + 930 * 60);
  rds_clock_sync_update(&sync, rds_clock_time(r), &clock, 902000); // Cached CT must not overwrite NTP.
  assert(device_clock_read(&clock, 902000).source == TIME_SOURCE_NTP);
  device_clock_synchronize(&clock, 0, TIME_SOURCE_NONE, 903000);
  assert(device_clock_read(&clock, 903000).utc_seconds == ntp_utc + 3);
  // A genuinely new CT remains another caller of the same synchronization API.
  ct(r, 61327, 23, 59, 0); rds_clock_sync_update(&sync, rds_clock_time(r), &clock, 1000000);
  local = device_clock_read(&clock, 1060000);
  assert(local.source == TIME_SOURCE_RDS && local.hour == 0 && local.minute == 0);
  assert(local.local_seconds / 86400 + 40587 == 61328);
  // The 64-bit HAL timebase survives both a 32-bit millis boundary and many wraps.
  device_clock_t long_running = {0};
  const uint64_t started = ((uint64_t)1 << 32) - 500;
  device_clock_synchronize(&long_running, 0, TIME_SOURCE_NTP, started);
  assert(device_clock_read(&long_running, started + 1000).utc_seconds == 1);
  assert(device_clock_read(&long_running, started + ((uint64_t)1 << 33) + 7000).utc_seconds == 8589941);
  assert(!device_clock_read(&long_running, started - 1).valid);
  device_clock_set_utc_offset(&long_running, -30);
  local = device_clock_read(&long_running, started);
  assert(local.valid && local.utc_seconds == 0 && local.local_seconds == -1800);
  assert(local.hour == 23 && local.minute == 30);
  device_clock_t rebooted = {0}; assert(!device_clock_read(&rebooted, started).valid);
  // PS and raw CT retain their existing independent invalidation behavior.
  for (int repeat = 0; repeat < 2; ++repeat)
    for (int segment = 0; segment < 4; ++segment) rds_decoder_accept(r, 0x1234, segment, 0x4142, 0, 0);
  assert(rds_ps(r)[0] && rds_clock_time(r)->valid);
  rds_decoder_accept(r, 0x4321, 0, 0, 0, 0); assert(!rds_clock_time(r)->valid && device_clock_read(&clock, 1100000).valid);
  puts("CT decoding, retained device time, duplicate/station handling, shared NTP API, offsets and long uptime passed");
  return 0;
}

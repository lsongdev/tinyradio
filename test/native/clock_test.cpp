#include "drivers/si473x/rds.h"
#include "app/rds_clock.h"
#include "services/clock.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>
using drivers::RdsDecoder;
static void ct(RdsDecoder& r, uint32_t mjd, unsigned hour, unsigned minute, int halfHours,
               uint8_t errors = 0, bool versionB = false, uint16_t pi = 0x1234) {
  const uint16_t b = 0x4000 | (versionB ? 0x800 : 0) | (mjd >> 15);
  const uint16_t c = ((mjd & 32767) << 1) | (hour >> 4);
  const unsigned offset = halfHours < 0 ? (32 | -halfHours) : halfHours;
  r.accept(pi, b, (hour << 12) | (minute << 6) | offset, errors, c);
}
int main() {
  RdsDecoder r; services::Clock clock; app::RdsClockSync sync;
  assert(!r.clockTime().valid && !clock.read(0).valid);
  ct(r, 61327, 16, 45, 16, 0xaa); // Corrected blocks; C carries hour bit 4.
  const auto t = r.clockTime();
  assert(t.valid && t.mjd == 61327 && t.utcMinutes == 1005 && t.offsetMinutes == 480);
  sync.update(t, clock, 1000);
  auto local = clock.read(1000);
  const int64_t utc = (int64_t(61327) - 40587) * 86400 + 1005 * 60;
  assert(local.valid && local.utcSeconds == utc && local.utcOffsetMinutes == 480);
  assert(local.localSeconds / 86400 + 40587 == 61328 && local.hour == 0 && local.minute == 45);
  assert(clock.read(61000).minute == 46);
  assert(clock.read(601000).valid && clock.read(601000).minute == 55); // No expiry.
  // Re-reading an old sample, even with a new sequence or PI, cannot rewind.
  sync.update(t,clock,601000);
  ct(r,61327,16,45,16); sync.update(r.clockTime(),clock,601000);
  assert(clock.read(601000).utcSeconds == utc + 600);
  r.clear(); sync.update(r.clockTime(),clock,602000);
  assert(clock.read(602000).valid); // Tuning/seek invalidates CT, not device time.
  ct(r,61327,16,45,16,0,false,0x4321); sync.update(r.clockTime(),clock,603000);
  assert(clock.read(603000).utcSeconds == utc + 602);
  ct(r,0,0,0,0,0,false,0x4321); sync.update(r.clockTime(),clock,604000);
  assert(!r.clockTime().valid && clock.read(604000).utcSeconds == utc + 603);
  ct(r,61327,16,56,16); sync.update(r.clockTime(),clock,605000);
  assert(clock.read(605000).hour == 0 && clock.read(605000).minute == 56);
  // An offset-only change preserves the running seconds.
  ct(r,61327,16,56,-1); sync.update(r.clockTime(),clock,615000);
  assert(clock.read(615000).hour == 16 && clock.read(615000).minute == 26 && clock.read(615000).second == 10);
  for (uint8_t error : {uint8_t(0xc0),uint8_t(0x30),uint8_t(0x0c),uint8_t(3)}) {
    const auto seq = r.clockTime().sequence;
    ct(r,61327,18,12,0,error); assert(r.clockTime().sequence == seq);
  }
  const auto seq = r.clockTime().sequence;
  ct(r,61327,24,0,0); ct(r,61327,23,60,0);
  ct(r,100000,0,0,0); ct(r,0,1,0,0); ct(r,61327,12,0,0,0,true);
  assert(r.clockTime().sequence == seq);
  for (int offset : {-31,0,31}) {
    ct(r,65536,12,59,offset); assert(r.clockTime().mjd == 65536 && r.clockTime().offsetMinutes == offset * 30);
  }
  ct(r,61327,0,15,-1); sync.update(r.clockTime(),clock,700000);
  local=clock.read(700000);
  assert(local.localSeconds / 86400 + 40587 == 61326 && local.hour == 23 && local.minute == 45);
  ct(r,99999,23,59,31); sync.update(r.clockTime(),clock,800000);
  local=clock.read(800000);
  assert(local.localSeconds / 86400 + 40587 == 100000 && local.hour == 15 && local.minute == 29);
  // Future NTP uses the identical UTC interface and UI reading, retaining timezone.
  const int64_t ntpUtc=4102444800LL; // Beyond 2038; no 32-bit epoch truncation.
  clock.synchronize(ntpUtc,services::TimeSource::Ntp,900000);
  local=clock.read(901234);
  assert(local.valid && local.source == services::TimeSource::Ntp && local.utcSeconds == ntpUtc+1);
  assert(local.utcOffsetMinutes == 930 && local.localSeconds == ntpUtc+1+930*60);
  sync.update(r.clockTime(),clock,902000); // Cached CT must not overwrite NTP.
  assert(clock.read(902000).source == services::TimeSource::Ntp);
  clock.synchronize(0,services::TimeSource::None,903000);
  assert(clock.read(903000).utcSeconds == ntpUtc+3);
  // A genuinely new CT remains another caller of the same synchronization API.
  ct(r,61327,23,59,0); sync.update(r.clockTime(),clock,1000000);
  local=clock.read(1060000);
  assert(local.source == services::TimeSource::Rds && local.hour == 0 && local.minute == 0);
  assert(local.localSeconds / 86400 + 40587 == 61328);
  // The 64-bit HAL timebase survives both a 32-bit millis boundary and many wraps.
  services::Clock longRunning;
  const uint64_t started=(uint64_t(1)<<32)-500;
  longRunning.synchronize(0,services::TimeSource::Ntp,started);
  assert(longRunning.read(started+1000).utcSeconds == 1);
  assert(longRunning.read(started+(uint64_t(1)<<33)+7000).utcSeconds == 8589941);
  assert(!longRunning.read(started-1).valid);
  longRunning.setUtcOffsetMinutes(-30);
  local=longRunning.read(started);
  assert(local.valid && local.utcSeconds == 0 && local.localSeconds == -1800);
  assert(local.hour == 23 && local.minute == 30);
  services::Clock rebooted; assert(!rebooted.read(started).valid);
  // PS and raw CT retain their existing independent invalidation behavior.
  for(int repeat=0;repeat<2;++repeat) for(int segment=0;segment<4;++segment) r.accept(0x1234,segment,0x4142,0);
  assert(r.text()[0] && r.clockTime().valid);
  r.accept(0x4321,0,0,0); assert(!r.clockTime().valid && clock.read(1100000).valid);
  puts("CT decoding, retained device time, duplicate/station handling, shared NTP API, offsets and long uptime passed");
}

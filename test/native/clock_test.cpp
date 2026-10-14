#include "drivers/si473x/rds.h"
#include "radio/clock.h"
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
  RdsDecoder r; radio::Clock clock;
  assert(!r.clockTime().valid && !clock.read(0).valid);
  ct(r, 61327, 16, 45, 16, 0xaa); // All blocks corrected; C carries hour bit 4.
  const auto t = r.clockTime();
  assert(t.valid && t.mjd == 61327 && t.utcMinutes == 1005 && t.offsetMinutes == 480);
  clock.update(t, 1000);
  auto local = clock.read(1000);
  assert(local.valid && local.localMjd == 61328 && local.minutes == 45);
  assert(clock.read(61000).minutes == 46);
  assert(clock.read(300999).valid && !clock.read(301000).valid);
  clock.update(t, 301000); assert(!clock.read(301000).valid);
  // Expired time must not reappear after a complete millis wrap.
  clock.update(t, 1001); assert(!clock.read(1001).valid);
  ct(r, 61327, 16, 46, 16); clock.update(r.clockTime(), 400000);
  assert(clock.read(400000).valid && clock.read(400000).minutes == 46);
  // A duplicate decoded group does not move the running clock backwards.
  ct(r, 61327, 16, 46, 16); clock.update(r.clockTime(), 460000);
  assert(clock.read(460000).minutes == 47);
  for (uint8_t error : {uint8_t(0xc0),uint8_t(0x30),uint8_t(0x0c),uint8_t(3)}) {
    const auto seq = r.clockTime().sequence;
    ct(r, 61327, 18, 12, 0, error); assert(r.clockTime().sequence == seq);
  }
  const auto seq = r.clockTime().sequence;
  ct(r, 61327, 24, 0, 0); ct(r, 61327, 23, 60, 0);
  ct(r, 100000, 0, 0, 0); ct(r, 0, 1, 0, 0); ct(r, 61327, 12, 0, 0, 0, true);
  assert(r.clockTime().sequence == seq);
  ct(r, 61327, 0, 15, -1); clock.update(r.clockTime(), 0);
  local = clock.read(0); assert(local.localMjd == 61326 && local.minutes == 1425);
  for (int offset : {-31,0,31}) {
    ct(r, 65536, 12, 59, offset); // Both high MJD bits must survive B/C assembly.
    assert(r.clockTime().mjd == 65536 && r.clockTime().offsetMinutes == offset * 30);
  }
  ct(r, 99999, 23, 59, 31); clock.update(r.clockTime(), 1000);
  local = clock.read(1000); assert(local.localMjd == 100000 && local.minutes == 929);
  ct(r, 61327, 23, 59, 0); clock.clear(); clock.update(r.clockTime(), 0xffffff00u);
  local = clock.read(uint32_t(0xffffff00u + 60000u));
  assert(local.valid && local.localMjd == 61328 && local.minutes == 0);
  // PS and clock updates are independent.
  for (int repeat = 0; repeat < 2; ++repeat) for (int segment = 0; segment < 4; ++segment)
    r.accept(0x1234,segment,0x4142,0);
  assert(r.text()[0] && r.clockTime().valid);
  ct(r,0,0,0,0); assert(!r.clockTime().valid && r.text()[0]);
  clock.update(r.clockTime(), 100); assert(!clock.read(100).valid);
  ct(r,61327,12,0,0); r.accept(0x4321,0,0,0); assert(!r.clockTime().valid);
  ct(r,61327,12,0,0); r.clear(); assert(!r.clockTime().valid);
  // A station change can clear and receive new CT in the same FIFO batch.
  // Its sequence may equal the previous station's; PI still forces an update.
  r.clear(); clock.clear(); ct(r,61327,12,0,0); clock.update(r.clockTime(),0);
  ct(r,61327,18,0,0,0,false,0x4321); clock.update(r.clockTime(),1000);
  assert(clock.read(1000).minutes == 1080 && r.clockTime().pi == 0x4321);
  puts("RDS CT fields, UTC/local rollover, invalid blocks, withdrawal, duplicates, holdover and millis wrap passed");
}

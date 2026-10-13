#include "drivers/si473x/rds.h"
#include "drivers/si473x/settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
using drivers::RdsDecoder;
static void ps(RdsDecoder& r, uint16_t pi, const char* name, uint8_t errors = 0, bool versionB = false) {
  for (int i = 3; i >= 0; --i) r.accept(pi, (versionB ? 0x800 : 0) | i,
    uint16_t(uint8_t(name[i * 2]) << 8 | uint8_t(name[i * 2 + 1])), errors);
}
static void rt(RdsDecoder& r, const char* text, bool flag = false, bool versionB = false, uint8_t errors = 0) {
  char data[64]; memset(data, ' ', sizeof data);
  const int n = strlen(text), capacity = versionB ? 32 : 64; assert(n <= capacity);
  memcpy(data, text, n); if (n < capacity) data[n] = 13;
  const int width = versionB ? 2 : 4;
  for (int i = 0; i < capacity / width && i <= n / width; ++i) {
    const uint16_t c = uint8_t(data[i * width]) << 8 | uint8_t(data[i * width + 1]);
    const uint16_t d = versionB ? c : uint8_t(data[i * width + 2]) << 8 | uint8_t(data[i * width + 3]);
    r.accept(0x1234, 0x2000 | (versionB ? 0x800 : 0) | (flag ? 0x10 : 0) | i, d, errors, c);
  }
}
int main() {
  RdsDecoder r;
  ps(r, 0x1234, "RADIO   "); assert(!r.text()[0]);
  ps(r, 0x1234, "RADIO   ", 0, true); assert(!strcmp(r.text(), "RADIO"));
  ps(r, 0x4321, "NEW FM  "); assert(!r.text()[0]);
  ps(r, 0x4321, "NEW FM  "); assert(!strcmp(r.text(), "NEW FM"));
  r.clear();
  ps(r, 0x1234, "BAD FM  ", 0x30); ps(r, 0x1234, "BAD FM  ", 3); assert(!r.text()[0]);
  ps(r, 0x1234, "GOOD FM ", 0x51); ps(r, 0x1234, "GOOD FM ", 0x51); assert(!strcmp(r.text(), "GOOD FM"));
  r.clear(); ps(r, 0x1234, "CORRECT ", 0xaa); ps(r, 0x1234, "CORRECT ", 0xaa);
  assert(!strcmp(r.text(), "CORRECT")); // BLE=2 is corrected, not uncorrectable.
  r.clear(); r.accept(0x1234, 0, 0x4142, 0); r.accept(0x1234, 1, 0x4344, 0);
  assert(!r.text()[0]); r.clear(); ps(r, 0x1234, "RESTORED"); assert(!r.text()[0]);
  ps(r, 0x1234, "RESTORED"); assert(!strcmp(r.text(), "RESTORED"));
  const char* message = "Long radio text scrolls across the main display.";
  rt(r, message); assert(!r.radioText()[0]); rt(r, message); assert(!strcmp(r.radioText(), message));
  rt(r, "New programme", true); assert(!r.radioText()[0]);
  rt(r, "New programme", true); assert(!strcmp(r.radioText(), "New programme"));
  rt(r, "Version B", true, true); assert(!r.radioText()[0]);
  rt(r, "Version B", true, true); assert(!strcmp(r.radioText(), "Version B"));
  r.clear(); rt(r, "Bad C", false, false, 0x0c); rt(r, "Bad C", false, false, 0x0c); assert(!r.radioText()[0]);
  rt(r, "Valid C"); rt(r, "Valid C"); assert(!strcmp(r.radioText(), "Valid C"));
  char full[65]; memset(full, 'X', 64); full[64] = 0;
  rt(r, full, true); rt(r, full, true); assert(strlen(r.radioText()) == 64);
  char shortFull[33]; memset(shortFull, 'Y', 32); shortFull[32] = 0;
  rt(r, shortFull, false, true); rt(r, shortFull, false, true); assert(strlen(r.radioText()) == 32);
  ps(r, 0x4321, "OTHER FM"); assert(!r.radioText()[0]);
  for (auto mode : {drivers::Mode::FM, drivers::Mode::AM, drivers::Mode::LSB, drivers::Mode::USB}) {
    int count; const auto* bw = drivers::bandwidths(mode, count);
    for (int i = 1; i < count; ++i) assert(bw[i].hz > bw[i - 1].hz);
  }
  puts("RDS PS/RT tests passed");
}

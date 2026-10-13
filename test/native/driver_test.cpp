#include "drivers/si473x/si473x.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool has(const TwoWire& bus, std::initializer_list<uint8_t> bytes) {
  for (const auto& cmd : bus.commands) if (cmd == std::vector<uint8_t>(bytes)) return true;
  return false;
}
static void name(TwoWire& bus, const char* text) {
  for (int repeat = 0; repeat < 2; ++repeat) for (int i = 0; i < 4; ++i) {
    std::array<uint8_t,13> r{}; r[0] = 0x80; r[2] = 1;
    r[4] = 0x12; r[5] = 0x34; r[7] = i; r[10] = text[i * 2]; r[11] = text[i * 2 + 1]; bus.fifo.push_back(r);
  }
}
static void group(TwoWire& bus, uint16_t b, uint16_t c, uint16_t d) {
  std::array<uint8_t,13> r{}; r[0] = 0x80; r[2] = 1;
  r[4] = 0x12; r[5] = 0x34;
  r[6] = b >> 8; r[7] = b; r[8] = c >> 8; r[9] = c; r[10] = d >> 8; r[11] = d;
  bus.fifo.push_back(r);
}
static void enhanced(TwoWire& bus) {
  group(bus, (6 << 11) | 22, 1, 0x6552); // 3A announces UTF-8 in 11A.
  // Chinese UTF-8 bytes cross the four-byte segment boundary.
  for (int repeat = 0; repeat < 2; ++repeat) {
    group(bus, 22 << 11, 0xe4b8, 0xade6);
    group(bus, (22 << 11) | 1, 0x9687, 0x0d20);
  }
}
static bool settle(drivers::Si473x& chip) {
  for (int i = 0; i < 20 && chip.tuning(); ++i) if (!chip.poll()) return false;
  return !chip.tuning();
}
int main() {
  TwoWire bus; drivers::Si473x chip(bus);
  assert(chip.begin() && chip.configure(drivers::Mode::FM, 18));
  assert(has(bus, {0x12,0,0x15,2,0xaa,1}));
  assert(has(bus, {0x12,0,0x15,0,0,1}) && has(bus, {0x12,0,0x15,1,0,1}));
  assert(chip.tune(100000000) && settle(chip));
  assert(chip.setReception({1,26,31})); assert(chip.bandwidthHz() == 40000);
  assert(has(bus, {0x12,0,0x11,2,0,4})); assert(has(bus, {0x28,1,26}));
  assert(has(bus, {0x12,0,0x13,2,0,31}));
  name(bus, "RADIO FM"); assert(chip.readRds() && bus.fifo.empty() && !strcmp(chip.rdsPs(), "RADIO FM"));
  assert(chip.rdsStatus().groups == 8 && chip.rdsStatus().synchronized);
  assert(chip.readRds() && !strcmp(chip.rdsPs(), "RADIO FM")); // Empty FIFO must not inject a phantom group.
  bus.synced = false; assert(chip.readRds() && !strcmp(chip.rdsPs(), "RADIO FM"));
  assert(!chip.rdsStatus().synchronized); bus.synced = true;
  name(bus, "RADIO FM"); assert(chip.readRds() && chip.readRds());
  enhanced(bus); assert(chip.readRds() && bus.fifo.empty());
  assert(chip.rdsEnhancedGroup() == 22 && chip.rdsEnhancedFlags() == 1);
  assert(!strcmp(chip.rdsEnhancedText(), "中文") && !strcmp(chip.rdsPs(), "RADIO FM"));
  assert(chip.readRds() && !strcmp(chip.rdsEnhancedText(), "中文"));
  name(bus, "OLD NAME"); assert(chip.tune(100100000) && !chip.rdsPs()[0] && bus.fifo.empty()); assert(settle(chip));
  assert(chip.rdsEnhancedGroup() == -1 && !chip.rdsEnhancedText()[0]);
  enhanced(bus); assert(chip.readRds());
  name(bus, "RADIO FM"); assert(chip.readRds() && chip.readRds());
  assert(chip.seek(true,64000000,108000000,100000) && !chip.rdsPs()[0]); assert(!chip.rdsEnhancedText()[0] && chip.rdsEnhancedGroup() == -1); assert(chip.cancelSeek());
  assert(chip.configure(drivers::Mode::AM,18) && chip.setReception({0,37,8}));
  assert(chip.bandwidthHz() == 1000 && has(bus, {0x12,0,0x31,2,0,4}) && has(bus, {0x48,1,37}));
  assert(chip.configure(drivers::Mode::USB,18) && chip.setReception({0,-1,0}));
  assert(chip.bandwidthHz() == 500 && has(bus, {0x12,0,1,1,0x90,4}) && has(bus, {0x48,0,0}));
  assert(chip.setReception({3,-1,0}) && has(bus, {0x12,0,1,1,0x90,0x11}));
  puts("Driver commands, RDS FIFO, tuning invalidation and reception settings passed");
}

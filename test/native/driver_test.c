#include "drivers/si473x/si473x.h"
#include "app/rds_clock.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Simulated SI473x: always CTS, echoes the tuned carrier and serves a scripted RDS FIFO.
#define MAX_COMMANDS 4096
#define MAX_GROUPS 64
typedef struct {
  struct { uint8_t size, bytes[8]; } commands[MAX_COMMANDS];
  size_t command_count;
  uint8_t fifo[MAX_GROUPS][13];
  size_t fifo_head, fifo_count;
  bool synced;
  uint8_t tx[8], tx_size;
  uint16_t carrier, volume, hard_mute;
} fake_bus_t;

static bool fake_write(void* context, uint8_t address, const uint8_t* data, size_t size) {
  fake_bus_t* bus = (fake_bus_t*)context; (void)address;
  assert(size <= sizeof bus->tx);
  memcpy(bus->tx, data, size); bus->tx_size = (uint8_t)size;
  if (size) {
    assert(bus->command_count < MAX_COMMANDS);
    bus->commands[bus->command_count].size = (uint8_t)size;
    memcpy(bus->commands[bus->command_count++].bytes, data, size);
    if (data[0] == 0x20 || data[0] == 0x40) bus->carrier = data[2] << 8 | data[3];
    if (size == 6 && data[0] == 0x12 && data[2] == 0x40) {
      const uint16_t value = data[4] << 8 | data[5];
      if (data[3] == 0) bus->volume = value;
      if (data[3] == 1) bus->hard_mute = value;
    }
    if (data[0] == 0x24 && data[1] == 3) bus->fifo_count = 0;
  }
  return true;
}
static bool fake_read(void* context, uint8_t address, uint8_t* data, size_t size) {
  fake_bus_t* bus = (fake_bus_t*)context; (void)address;
  memset(data, 0, size); data[0] = 0x81;
  const uint8_t cmd = bus->tx_size ? bus->tx[0] : 0;
  if (size == 9 && cmd == 0x10) { data[4] = 0xa9; data[5] = 2; }
  if (size == 8 && (cmd == 0x22 || cmd == 0x42)) { data[2] = bus->carrier >> 8; data[3] = (uint8_t)bus->carrier; }
  if (size == 13) {
    if (bus->tx_size > 1 && bus->tx[1] == 1 && bus->fifo_count) {
      memcpy(data, bus->fifo[bus->fifo_head], 13);
      bus->fifo_head = (bus->fifo_head + 1) % MAX_GROUPS; --bus->fifo_count;
    }
    data[2] = bus->synced ? 1 : 0; data[3] = (uint8_t)bus->fifo_count;
  }
  return true;
}
// True when the exact command bytes (given after the count) were sent.
static bool has(const fake_bus_t* bus, int size, ...) {
  uint8_t expected[8]; va_list args; va_start(args, size);
  for (int i = 0; i < size; ++i) expected[i] = (uint8_t)va_arg(args, int);
  va_end(args);
  for (size_t i = 0; i < bus->command_count; ++i)
    if (bus->commands[i].size == size && !memcmp(bus->commands[i].bytes, expected, size)) return true;
  return false;
}
static void group(fake_bus_t* bus, uint16_t b, uint16_t c, uint16_t d) {
  assert(bus->fifo_count < MAX_GROUPS);
  uint8_t* r = bus->fifo[(bus->fifo_head + bus->fifo_count++) % MAX_GROUPS];
  memset(r, 0, 13); r[0] = 0x80; r[2] = 1; r[4] = 0x12; r[5] = 0x34;
  r[6] = b >> 8; r[7] = (uint8_t)b; r[8] = c >> 8; r[9] = (uint8_t)c; r[10] = d >> 8; r[11] = (uint8_t)d;
}
static void name(fake_bus_t* bus, const char* text) {
  for (int repeat = 0; repeat < 2; ++repeat)
    for (int i = 0; i < 4; ++i) group(bus, (uint16_t)i, 0, (uint16_t)((uint8_t)text[i * 2] << 8 | (uint8_t)text[i * 2 + 1]));
}
static void enhanced(fake_bus_t* bus) {
  group(bus, (6 << 11) | 22, 1, 0x6552); // 3A announces UTF-8 in 11A.
  // Chinese UTF-8 bytes cross the four-byte segment boundary.
  for (int repeat = 0; repeat < 2; ++repeat) {
    group(bus, 22 << 11, 0xe4b8, 0xade6);
    group(bus, (22 << 11) | 1, 0x9687, 0x0d20);
  }
}
static bool settle(si473x_t* chip) {
  for (int i = 0; i < 20 && chip->tuning; ++i) if (!si473x_poll(chip)) return false;
  return !chip->tuning;
}
static bool reception(si473x_t* chip, uint8_t bandwidth, int8_t gain, uint8_t soft_mute_db) {
  const si473x_reception_t settings = {bandwidth, gain, soft_mute_db};
  return si473x_set_reception(chip, &settings);
}
static fake_bus_t fake;
int main(void) {
  fake_bus_t* bus = &fake; bus->synced = true;
  const i2c_bus_t interface = {bus, fake_write, fake_read};
  si473x_t driver, *chip = &driver; si473x_init(chip, interface);
  const rds_decoder_t* rds = &chip->rds;
  device_clock_t clock = {0}; rds_clock_sync_t sync = {0};
  assert(si473x_begin(chip) && si473x_configure(chip, SI473X_FM, 18));
  assert(bus->hard_mute == 3 && bus->volume == 18);
  assert(has(bus, 6, 0x12,0,0x15,2,0xaa,1));
  assert(has(bus, 6, 0x12,0,0x15,0,0,1) && has(bus, 6, 0x12,0,0x15,1,0,1));
  assert(si473x_tune(chip, 100000000) && settle(chip));
  assert(bus->hard_mute == 3); // Tuning must not unmute before the application is ready.
  assert(si473x_set_muted(chip, false) && bus->hard_mute == 0 && bus->volume == 18);
  assert(si473x_set_muted(chip, true) && bus->hard_mute == 3 && bus->volume == 18);
  assert(si473x_set_volume(chip, 23) && bus->hard_mute == 3 && bus->volume == 23);
  assert(si473x_set_muted(chip, false) && bus->hard_mute == 0 && bus->volume == 23);
  assert(si473x_configure(chip, SI473X_FM, 18) && bus->hard_mute == 3); // Same engine too.
  assert(si473x_tune(chip, 100000000) && settle(chip));
  assert(reception(chip, 1, 26, 31)); assert(chip->bandwidth_hz == 40000);
  assert(has(bus, 6, 0x12,0,0x11,2,0,4)); assert(has(bus, 3, 0x28,1,26));
  assert(has(bus, 6, 0x12,0,0x13,2,0,31));
  name(bus, "RADIO FM"); assert(si473x_read_rds(chip) && !bus->fifo_count && !strcmp(rds_ps(rds), "RADIO FM"));
  assert(chip->rds_status.groups == 8 && chip->rds_status.usable_groups == 8 && chip->rds_status.synchronized);
  assert(si473x_read_rds(chip) && !strcmp(rds_ps(rds), "RADIO FM")); // Empty FIFO must not inject a phantom group.
  assert(chip->rds_status.groups == 8 && chip->rds_status.usable_groups == 8);
  // Uncorrectable errors in any block must not drive reception activity.
  // Unsupported group types still count when their data is correctable.
  for (int block = 0; block < 4; ++block) {
    group(bus, 0xf000, 0, 0);
    bus->fifo[(bus->fifo_head + bus->fifo_count - 1) % MAX_GROUPS][12] = (uint8_t)(3 << (block * 2));
  }
  group(bus, 0xf000, 0, 0);
  bus->fifo[(bus->fifo_head + bus->fifo_count - 1) % MAX_GROUPS][12] = 0xaa; // BLE=2 in every block is usable.
  assert(si473x_read_rds(chip));
  assert(chip->rds_status.groups == 13 && chip->rds_status.usable_groups == 9);
  bus->synced = false; assert(si473x_read_rds(chip) && !strcmp(rds_ps(rds), "RADIO FM"));
  assert(!chip->rds_status.synchronized); bus->synced = true;
  name(bus, "RADIO FM"); assert(si473x_read_rds(chip) && si473x_read_rds(chip));
  group(bus, 0x4001, 0xdf1e, 0xcad0); // MJD 61327, 12:43 UTC, +8h.
  assert(si473x_read_rds(chip) && rds_clock_time(rds)->valid);
  assert(rds_clock_time(rds)->mjd == 61327 && rds_clock_time(rds)->utc_minutes == 763 && rds_clock_time(rds)->offset_minutes == 480);
  rds_clock_sync_update(&sync, rds_clock_time(rds), &clock, 1000);
  const int64_t first_time = device_clock_read(&clock, 1000).utc_seconds;
  assert(device_clock_read(&clock, 1000).valid);
  enhanced(bus); assert(si473x_read_rds(chip) && !bus->fifo_count);
  assert(rds_enhanced_group(rds) == 22 && rds_enhanced_flags(rds) == 1);
  assert(!strcmp(rds_enhanced_text(rds), "中文") && !strcmp(rds_ps(rds), "RADIO FM"));
  assert(si473x_read_rds(chip) && !strcmp(rds_enhanced_text(rds), "中文"));
  name(bus, "OLD NAME"); assert(si473x_tune(chip, 100100000) && !rds_ps(rds)[0] && !bus->fifo_count); assert(settle(chip));
  assert(rds_enhanced_group(rds) == -1 && !rds_enhanced_text(rds)[0] && !rds_clock_time(rds)->valid);
  assert(!chip->rds_status.usable_groups && !chip->rds_status.groups);
  rds_clock_sync_update(&sync, rds_clock_time(rds), &clock, 601000);
  assert(device_clock_read(&clock, 601000).valid && device_clock_read(&clock, 601000).utc_seconds == first_time + 600);
  enhanced(bus); assert(si473x_read_rds(chip));
  name(bus, "RADIO FM"); assert(si473x_read_rds(chip) && si473x_read_rds(chip));
  group(bus, 0x4001, 0xdf1e, 0xcad0); assert(si473x_read_rds(chip) && rds_clock_time(rds)->valid);
  assert(si473x_seek(chip, true, 64000000, 108000000, 100000) && !rds_ps(rds)[0]);
  assert(!rds_enhanced_text(rds)[0] && rds_enhanced_group(rds) == -1 && !rds_clock_time(rds)->valid);
  assert(!chip->rds_status.usable_groups && !chip->rds_status.groups);
  assert(si473x_cancel_seek(chip));
  rds_clock_sync_update(&sync, rds_clock_time(rds), &clock, 602000);
  assert(device_clock_read(&clock, 602000).valid && device_clock_read(&clock, 602000).utc_seconds == first_time + 601);
  assert(si473x_configure(chip, SI473X_AM, 18) && reception(chip, 0, 37, 8));
  assert(bus->hard_mute == 3 && bus->volume == 18);
  assert(chip->bandwidth_hz == 1000 && has(bus, 6, 0x12,0,0x31,2,0,4) && has(bus, 3, 0x48,1,37));
  assert(si473x_configure(chip, SI473X_USB, 18) && reception(chip, 0, -1, 0));
  assert(bus->hard_mute == 3 && bus->volume == 18);
  assert(chip->bandwidth_hz == 500 && has(bus, 6, 0x12,0,1,1,0x90,4) && has(bus, 3, 0x48,0,0));
  assert(reception(chip, 3, -1, 0) && has(bus, 6, 0x12,0,1,1,0x90,0x11));
  // The crystal configuration enables XOSCEN in every POWER_UP and never sets REFCLK.
  assert(has(bus, 3, 0x01,0x10,0x05) && has(bus, 3, 0x01,0x11,0x05) && has(bus, 3, 0x01,0x1f,0x05) && has(bus, 3, 0x01,0x31,0x05));
  assert(!has(bus, 6, 0x12,0,0x02,0x01,0x80,0x00));

  // External 32.768 kHz RCLK: no XOSCEN, then REFCLK_FREQ/PRESCALE.
  bus->command_count = 0;
  si473x_t external; si473x_init(&external, interface); external.external_clock = true;
  assert(si473x_begin(&external) && si473x_configure(&external, SI473X_FM, 18));
  assert(has(bus, 3, 0x01,0x00,0x05) && !has(bus, 3, 0x01,0x10,0x05));
  assert(has(bus, 6, 0x12,0,0x02,0x01,0x80,0x00) && has(bus, 6, 0x12,0,0x02,0x02,0,1));
  bus->command_count = 0;
  assert(si473x_configure(&external, SI473X_USB, 18) && external.revision.patch == 0xA902);
  assert(has(bus, 3, 0x01,0x0f,0x05) && has(bus, 3, 0x01,0x21,0x05));
  // Properties are only valid after the patch download.
  size_t last_patch = 0, reference = 0;
  for (size_t i = 0; i < bus->command_count; ++i) {
    const uint8_t* bytes = bus->commands[i].bytes;
    if (bytes[0] == 0x15 || bytes[0] == 0x16) last_patch = i;
    if (bus->commands[i].size == 6 && bytes[0] == 0x12 && bytes[2] == 0x02 && bytes[3] == 0x01) reference = i;
  }
  assert(last_patch && reference > last_patch);
  puts("Driver commands, RDS FIFO, tuning invalidation and reception settings passed");
  return 0;
}

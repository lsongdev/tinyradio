#include "drivers/pcf8523/pcf8523.h"
#include "drivers/sht3x/sht3x.h"
#include "services/calendar.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t registers[32], pointer; } fake_rtc_t;
static bool rtc_write(void* context, uint8_t address, const uint8_t* data, size_t size) {
  fake_rtc_t* chip = (fake_rtc_t*)context;
  if (address != 0x68 || !size) return false;
  chip->pointer = data[0];
  for (size_t i = 1; i < size; ++i) chip->registers[chip->pointer++] = data[i];
  return true;
}
static bool rtc_read(void* context, uint8_t address, uint8_t* data, size_t size) {
  fake_rtc_t* chip = (fake_rtc_t*)context;
  if (address != 0x68) return false;
  for (size_t i = 0; i < size; ++i) data[i] = chip->registers[chip->pointer++];
  return true;
}
static void calendar_round_trip(int64_t year, unsigned month, unsigned day, int64_t expected) {
  int64_t y; unsigned m, d;
  assert(calendar_days(year, month, day) == expected);
  calendar_date(expected, &y, &m, &d);
  assert(y == year && m == month && d == day);
}
int main(void) {
  calendar_round_trip(1970, 1, 1, 0);
  calendar_round_trip(2000, 3, 1, 11017);
  calendar_round_trip(2024, 2, 29, 19782);
  calendar_round_trip(2026, 10, 15, 20741);
  calendar_round_trip(1969, 12, 31, -1);
  for (int64_t days = -800000; days < 800000; days += 997) {
    int64_t y; unsigned m, d;
    calendar_date(days, &y, &m, &d); assert(calendar_days(y, m, d) == days);
  }
  assert(calendar_day_of(86399) == 0 && calendar_day_of(-1) == -1 && calendar_day_of(-86400) == -1 && calendar_day_of(-86401) == -2);

  // PCF8523 decoding: 2024-01-01 21:02:53, 24-hour, as read from the V5A.
  uint8_t raw[10] = {0x00, 0, 0x08, 0x53, 0x02, 0x21, 0x01, 0x01, 0x01, 0x24};
  pcf8523_time_t t; bool valid;
  pcf8523_decode(raw, &t, &valid);
  assert(valid && t.year == 2024 && t.month == 1 && t.day == 1 && t.hour == 21 && t.minute == 2 && t.second == 53);
  raw[3] |= 0x80; pcf8523_decode(raw, &t, &valid); assert(!valid); raw[3] &= 0x7f; // Oscillator stopped.
  raw[0] |= 0x20; pcf8523_decode(raw, &t, &valid); assert(!valid); raw[0] &= ~0x20; // STOP.
  raw[0] |= 0x08; raw[5] = 0x29; pcf8523_decode(raw, &t, &valid); assert(valid && t.hour == 21); // 9 PM.
  raw[5] = 0x12; pcf8523_decode(raw, &t, &valid); assert(valid && t.hour == 0); // 12 AM.
  raw[0] &= ~0x08; raw[5] = 0x24; pcf8523_decode(raw, &t, &valid); assert(!valid); // Hour 24.
  raw[5] = 0x21; raw[3] = 0x5a; pcf8523_decode(raw, &t, &valid); assert(!valid); // Bad BCD.
  raw[3] = 0x53; raw[6] = 0x29; raw[8] = 0x02; raw[9] = 0x23; pcf8523_decode(raw, &t, &valid); assert(!valid); // 2023-02-29.
  raw[9] = 0x24; pcf8523_decode(raw, &t, &valid); assert(valid); // 2024-02-29.

  // Writing: STOP while loading, 24-hour mode, OS cleared, Control_3 untouched.
  fake_rtc_t fake; memset(&fake, 0, sizeof fake);
  fake.registers[0] = 0x08; fake.registers[2] = 0x08; fake.registers[3] = 0x80;
  const pcf8523_t rtc = {{&fake, rtc_write, rtc_read}};
  const pcf8523_time_t set = {2026, 10, 15, 4, 12, 34, 56};
  assert(pcf8523_write(&rtc, &set));
  const uint8_t expected[] = {0x56, 0x34, 0x12, 0x15, 4, 0x10, 0x26};
  assert(!memcmp(fake.registers + 3, expected, sizeof expected) && fake.registers[0] == 0 && fake.registers[2] == 0x08);
  assert(pcf8523_read(&rtc, &t, &valid) && valid && !memcmp(&t, &set, sizeof t));
  const pcf8523_time_t bad = {2100, 1, 1, 0, 0, 0, 0};
  assert(!pcf8523_write(&rtc, &bad));

  // SHT3x: Sensirion's CRC example and conversion endpoints.
  const uint8_t beef[] = {0xBE, 0xEF};
  assert(sht3x_crc(beef, 2) == 0x92);
  uint8_t data[6] = {0, 0, 0, 0, 0, 0};
  data[2] = sht3x_crc(data, 2); data[5] = sht3x_crc(data + 3, 2);
  int32_t milli_c; uint32_t milli_rh;
  assert(sht3x_decode(data, &milli_c, &milli_rh) && milli_c == -45000 && milli_rh == 0);
  memset(data, 0xff, sizeof data); data[2] = sht3x_crc(data, 2); data[5] = sht3x_crc(data + 3, 2);
  assert(sht3x_decode(data, &milli_c, &milli_rh) && milli_c == 130000 && milli_rh == 100000);
  data[5] ^= 1; assert(!sht3x_decode(data, &milli_c, &milli_rh));
  puts("Calendar, PCF8523 decode/write and SHT3x CRC/conversion passed");
  return 0;
}

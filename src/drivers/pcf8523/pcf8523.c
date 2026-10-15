#include "pcf8523.h"
#include <string.h>

enum { ADDRESS = 0x68, REG_CONTROL1 = 0x00, REG_CONTROL3 = 0x02, REG_SECONDS = 0x03 };
enum { CONTROL1_STOP = 0x20, CONTROL1_12H = 0x08, SECONDS_OS = 0x80 };

static bool bcd(uint8_t raw, uint8_t maximum, uint8_t* value) {
  if ((raw & 15) > 9 || (raw >> 4) > 9) return false;
  *value = (uint8_t)((raw >> 4) * 10 + (raw & 15));
  return *value <= maximum;
}
static uint8_t to_bcd(uint8_t value) { return (uint8_t)((value / 10) << 4 | value % 10); }

void pcf8523_decode(const uint8_t raw[10], pcf8523_time_t* t, bool* valid) {
  uint8_t year = 0;
  memset(t, 0, sizeof *t);
  bool ok = bcd(raw[3] & 0x7F, 59, &t->second) && bcd(raw[4] & 0x7F, 59, &t->minute) &&
            bcd(raw[6] & 0x3F, 31, &t->day) && bcd(raw[8] & 0x1F, 12, &t->month) &&
            bcd(raw[9], 99, &year) && (raw[7] & 7) <= 6;
  t->year = (uint16_t)(2000 + year); t->weekday = raw[7] & 7;
  if (raw[0] & CONTROL1_12H) {
    uint8_t hour = 0;
    ok = ok && bcd(raw[5] & 0x1F, 12, &hour) && hour >= 1;
    t->hour = (uint8_t)(hour % 12 + ((raw[5] & 0x20) ? 12 : 0));
  } else ok = ok && bcd(raw[5] & 0x3F, 23, &t->hour);
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (!t->month || !t->day) ok = false;
  if (ok) ok = t->day <= days[t->month - 1] + (t->month == 2 && year % 4 == 0 ? 1 : 0);
  *valid = ok && !(raw[0] & CONTROL1_STOP) && !(raw[3] & SECONDS_OS);
}
static bool read_registers(const pcf8523_t* chip, uint8_t start, uint8_t* data, uint8_t count) {
  return chip->bus.write(chip->bus.context, ADDRESS, &start, 1) &&
         chip->bus.read(chip->bus.context, ADDRESS, data, count);
}
static bool write_registers(const pcf8523_t* chip, uint8_t start, const uint8_t* data, uint8_t count) {
  uint8_t frame[8];
  if (count > 7) return false;
  frame[0] = start; memcpy(frame + 1, data, count);
  return chip->bus.write(chip->bus.context, ADDRESS, frame, count + 1);
}
bool pcf8523_read(const pcf8523_t* chip, pcf8523_time_t* time, bool* valid) {
  uint8_t raw[10] = {0};
  if (!read_registers(chip, REG_CONTROL1, raw, 1) || !read_registers(chip, REG_CONTROL3, raw + 2, 8)) return false;
  pcf8523_decode(raw, time, valid);
  return true;
}
bool pcf8523_write(const pcf8523_t* chip, const pcf8523_time_t* t) {
  if (t->year < 2000 || t->year > 2099 || t->month < 1 || t->month > 12 || t->day < 1 || t->day > 31 ||
      t->hour > 23 || t->minute > 59 || t->second > 59 || t->weekday > 6) return false;
  uint8_t control;
  if (!read_registers(chip, REG_CONTROL1, &control, 1)) return false;
  // Stop the clock while loading so the new second starts from zero; 24-hour mode.
  const uint8_t stopped = (uint8_t)((control & ~CONTROL1_12H) | CONTROL1_STOP);
  const uint8_t calendar[] = {to_bcd(t->second), to_bcd(t->minute), to_bcd(t->hour), to_bcd(t->day),
                              t->weekday, to_bcd(t->month), to_bcd((uint8_t)(t->year - 2000))};
  const uint8_t running = (uint8_t)(stopped & ~CONTROL1_STOP);
  return write_registers(chip, REG_CONTROL1, &stopped, 1) &&
         write_registers(chip, REG_SECONDS, calendar, sizeof calendar) && // Seconds bit 7 = 0 clears OS.
         write_registers(chip, REG_CONTROL1, &running, 1);
}

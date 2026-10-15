#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "drivers/i2c_bus.h"
// PCF8523 real-time clock at 0x68. The chip keeps no time zone.
typedef struct { i2c_bus_t bus; } pcf8523_t;
typedef struct {
  uint16_t year;   // 2000..2099; the chip stores two digits.
  uint8_t month, day, weekday, hour, minute, second;
} pcf8523_time_t;
// Registers 0x00..0x09 as read by pcf8523_read(); raw[1] (Control_2) is unused.
// *valid requires a well-formed calendar, a running oscillator and a clear OS flag.
void pcf8523_decode(const uint8_t raw[10], pcf8523_time_t* time, bool* valid);
// False on a bus error. Skips Control_2: reading it clears the watchdog flag.
bool pcf8523_read(const pcf8523_t* chip, pcf8523_time_t* time, bool* valid);
// Sets the calendar in 24-hour mode, restarts the oscillator and clears OS.
// Control_3 (battery switch-over) is left as configured.
bool pcf8523_write(const pcf8523_t* chip, const pcf8523_time_t* time);

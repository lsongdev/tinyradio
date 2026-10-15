#pragma once
#include <stdbool.h>
#include <stdint.h>
// Board-independent supply reading; each board decides how to measure it.
typedef struct {
  uint32_t adc_millivolts;
  float volts;
  uint8_t estimated_percent;
  bool external_power_likely;
  bool plausible;
} hal_battery_reading_t;

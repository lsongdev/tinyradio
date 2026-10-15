#pragma once
#include <stdbool.h>
#include <stdint.h>
// Board-independent temperature and relative humidity reading.
typedef struct {
  bool valid;
  int32_t milli_celsius;
  uint32_t milli_percent;
} hal_environment_reading_t;

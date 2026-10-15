#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// Shortest exact text in MHz (>= 1 MHz) or kHz, without trailing zeros.
typedef struct { char number[16]; const char* unit; } radio_frequency_text_t;
static inline radio_frequency_text_t radio_format_frequency(uint32_t hz) {
  const int megahertz = hz >= 1000000;
  const uint32_t divisor = megahertz ? 1000000 : 1000;
  radio_frequency_text_t result;
  memset(&result, 0, sizeof result);
  result.unit = megahertz ? "MHz" : "kHz";
  if (hz % divisor) {
    snprintf(result.number, sizeof result.number, megahertz ? "%lu.%06lu" : "%lu.%03lu",
             (unsigned long)(hz / divisor), (unsigned long)(hz % divisor));
    size_t length = strlen(result.number);
    while (result.number[length - 1] == '0') result.number[--length] = 0;
  } else snprintf(result.number, sizeof result.number, "%lu", (unsigned long)(hz / divisor));
  return result;
}

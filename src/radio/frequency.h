#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
namespace radio {
struct FormattedFrequency { char number[16]; const char* unit; };
inline FormattedFrequency formatFrequency(uint32_t hz) {
  const bool megahertz = hz >= 1000000;
  const uint32_t divisor = megahertz ? 1000000 : 1000;
  FormattedFrequency result{}; result.unit = megahertz ? "MHz" : "kHz";
  if (hz % divisor) {
    snprintf(result.number, sizeof result.number, megahertz ? "%lu.%06lu" : "%lu.%03lu",
             static_cast<unsigned long>(hz / divisor), static_cast<unsigned long>(hz % divisor));
    size_t length = strlen(result.number);
    while (result.number[length - 1] == '0') result.number[--length] = 0;
  } else snprintf(result.number, sizeof result.number, "%lu", static_cast<unsigned long>(hz / divisor));
  return result;
}
}

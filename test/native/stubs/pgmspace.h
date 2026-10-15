#pragma once
#include <stdint.h>
#define PROGMEM
typedef uint8_t byte;
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))

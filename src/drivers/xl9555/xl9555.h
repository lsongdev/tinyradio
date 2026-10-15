#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "drivers/i2c_bus.h"
// XL9555 16-bit I/O expander (PCA9555 register map).
enum {
  XL9555_INPUT0 = 0, XL9555_INPUT1 = 1,
  XL9555_OUTPUT0 = 2, XL9555_OUTPUT1 = 3,
  XL9555_POLARITY0 = 4, XL9555_POLARITY1 = 5,
  XL9555_CONFIG0 = 6, XL9555_CONFIG1 = 7, // 1 = input, 0 = output.
};
typedef struct {
  i2c_bus_t bus;
  uint8_t address;
} xl9555_t;
bool xl9555_read(const xl9555_t* chip, uint8_t reg, uint8_t* value);
bool xl9555_write(const xl9555_t* chip, uint8_t reg, uint8_t value);
// Read-modify-write of the bits in mask, verified by readback.
bool xl9555_update(const xl9555_t* chip, uint8_t reg, uint8_t mask, uint8_t value);
// Drive the masked pins of one port as outputs at the given level. The output
// latch is written before the direction so a pin never glitches to the old level.
bool xl9555_set_outputs(const xl9555_t* chip, uint8_t port, uint8_t mask, uint8_t value);

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "drivers/i2c_bus.h"
// Si5351A clock generator with a 25 MHz crystal.
typedef struct {
  i2c_bus_t bus;
  uint8_t address;
} si5351_t;
bool si5351_read(const si5351_t* chip, uint8_t reg, uint8_t* bytes, uint8_t count);
// Writes up to eight consecutive registers and verifies them by readback.
bool si5351_write(const si5351_t* chip, uint8_t reg, const uint8_t* bytes, uint8_t count);
// Drives CLK1 at exactly 32768 Hz from PLLB, for a receiver's reference clock.
// PLLA and the other multisynths are left alone; CLK0's output is disabled.
bool si5351_start_clk1_32768(const si5351_t* chip);
bool si5351_disable_outputs(const si5351_t* chip);
// Drives CLK0 at hz (about 100-150 MHz) from PLLA with an integer MS0 = 6,
// waiting up to 300 ms for PLLA to lock. CLK1/PLLB are left running.
bool si5351_set_clk0(const si5351_t* chip, uint32_t hz);
bool si5351_disable_clk0(const si5351_t* chip);

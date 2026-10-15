#include "si5351.h"
#include <Arduino.h>
#include <string.h>

enum {
  REG_STATUS = 0, REG_OUTPUT_ENABLE = 3, REG_PLL_SOURCE = 15, REG_CLK1_CONTROL = 17,
  REG_PLLB = 34, REG_MS1 = 50, REG_SPREAD = 149, REG_PLL_RESET = 177, REG_CRYSTAL_LOAD = 183,
};
enum { STATUS_SYS_INIT = 0x80, STATUS_LOL_B = 0x40 };

bool si5351_read(const si5351_t* chip, uint8_t reg, uint8_t* bytes, uint8_t count) {
  return chip->bus.write(chip->bus.context, chip->address, &reg, 1) &&
         chip->bus.read(chip->bus.context, chip->address, bytes, count);
}
static bool write_raw(const si5351_t* chip, uint8_t reg, const uint8_t* bytes, uint8_t count) {
  uint8_t frame[9];
  if (count > 8) return false;
  frame[0] = reg; memcpy(frame + 1, bytes, count);
  return chip->bus.write(chip->bus.context, chip->address, frame, count + 1);
}
bool si5351_write(const si5351_t* chip, uint8_t reg, const uint8_t* bytes, uint8_t count) {
  uint8_t actual[8];
  return write_raw(chip, reg, bytes, count) && si5351_read(chip, reg, actual, count) && !memcmp(bytes, actual, count);
}
static bool write_byte(const si5351_t* chip, uint8_t reg, uint8_t value) { return si5351_write(chip, reg, &value, 1); }

bool si5351_start_clk1_32768(const si5351_t* chip) {
  // PLLB = 25 MHz x 36 = 900 MHz (integer: P1 = 128 x 36 - 512, P2 = 0, P3 = 1).
  static const uint8_t pll[] = {0, 1, 0, 0x10, 0, 0, 0, 0};
  // MS1 = 214 + 37796/65536, R1 = 128: 900 MHz / 214.5767... / 128 = 32768 Hz.
  enum { A = 214, B = 37796, C = 65536, F = 128 * B / C };
  const uint32_t p1 = 128 * A + F - 512, p2 = 128 * B - (uint32_t)C * F;
  const uint8_t ms[] = {(uint8_t)(C >> 8), (uint8_t)C, (uint8_t)(0x70 | (p1 >> 16)), (uint8_t)(p1 >> 8), (uint8_t)p1,
                        (uint8_t)(((C >> 12) & 0xF0) | ((p2 >> 16) & 0x0F)), (uint8_t)(p2 >> 8), (uint8_t)p2};
  uint8_t enable, source, spread, status;
  if (!si5351_read(chip, REG_OUTPUT_ENABLE, &enable, 1) || !si5351_read(chip, REG_PLL_SOURCE, &source, 1) ||
      !si5351_read(chip, REG_SPREAD, &spread, 1)) return false;
  // Disable CLK0/CLK1 while reprogramming; 10 pF crystal load; both PLLs from the crystal; no spread spectrum.
  if (!write_byte(chip, REG_OUTPUT_ENABLE, enable | 3) || !write_byte(chip, REG_CRYSTAL_LOAD, 0x92) ||
      !write_byte(chip, REG_PLL_SOURCE, source & ~0x0C) || !write_byte(chip, REG_SPREAD, spread & ~0x80) ||
      !si5351_write(chip, REG_PLLB, pll, sizeof pll) || !si5351_write(chip, REG_MS1, ms, sizeof ms) ||
      !write_byte(chip, REG_CLK1_CONTROL, 0x2F)) return false; // Powered, MS1 from PLLB, 8 mA.
  // PLL reset is self-clearing, so it is written without readback.
  const uint8_t reset_pllb = 0x80;
  if (!write_raw(chip, REG_PLL_RESET, &reset_pllb, 1)) return false;
  delay(20);
  if (!si5351_read(chip, REG_STATUS, &status, 1) || (status & (STATUS_SYS_INIT | STATUS_LOL_B))) return false;
  // Output enable is active low: enable CLK1, keep CLK0 off.
  return write_byte(chip, REG_OUTPUT_ENABLE, (uint8_t)((enable | 1) & ~2));
}
bool si5351_disable_outputs(const si5351_t* chip) { return write_byte(chip, REG_OUTPUT_ENABLE, 0xff); }

#include "drivers/si5351/si5351.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Register file with auto-increment, a self-clearing PLL reset, and lock status.
typedef struct { uint8_t registers[256], pointer; bool reset_pllb; bool locked; } fake_clock_t;
static bool fake_write(void* context, uint8_t address, const uint8_t* data, size_t size) {
  fake_clock_t* chip = (fake_clock_t*)context;
  if (address != 0x60 || !size) return false;
  chip->pointer = data[0];
  for (size_t i = 1; i < size; ++i) {
    if (chip->pointer == 177) { chip->reset_pllb = data[i] & 0x80; ++chip->pointer; continue; }
    chip->registers[chip->pointer++] = data[i];
  }
  return true;
}
static bool fake_read(void* context, uint8_t address, uint8_t* data, size_t size) {
  fake_clock_t* chip = (fake_clock_t*)context;
  if (address != 0x60) return false;
  for (size_t i = 0; i < size; ++i) {
    const uint8_t reg = chip->pointer++;
    data[i] = reg == 0 ? (chip->locked ? 0 : 0x40) : chip->registers[reg];
  }
  return true;
}
int main(void) {
  static fake_clock_t fake;
  memset(&fake, 0, sizeof fake);
  fake.registers[3] = 0xff; fake.registers[15] = 0x0c; fake.registers[149] = 0x80; fake.locked = true;
  const si5351_t chip = {{&fake, fake_write, fake_read}, 0x60};
  assert(si5351_start_clk1_32768(&chip));
  // PLLB 900 MHz, MS1 214 + 37796/65536 with R1 = 128 (values from the verified V5A firmware).
  const uint8_t pll[] = {0, 1, 0, 0x10, 0, 0, 0, 0};
  const uint8_t ms[] = {0x00, 0x00, 0x70, 0x69, 0x49, 0x10, 0xd2, 0x00};
  assert(!memcmp(fake.registers + 34, pll, sizeof pll) && !memcmp(fake.registers + 50, ms, sizeof ms));
  assert(fake.registers[17] == 0x2f && fake.registers[183] == 0x92);
  assert(fake.registers[15] == 0 && fake.registers[149] == 0 && fake.reset_pllb);
  assert(fake.registers[3] == 0xfd); // CLK1 on, CLK0 and the rest off.
  // An unlocked PLLB fails before enabling the output.
  memset(&fake, 0, sizeof fake);
  fake.registers[3] = 0xff;
  assert(!si5351_start_clk1_32768(&chip) && fake.registers[3] == 0xff);
  puts("Si5351 32.768 kHz reference: PLLB, MS1, output enable and lock check passed");
  return 0;
}

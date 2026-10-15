#include "xl9555.h"
bool xl9555_read(const xl9555_t* chip, uint8_t reg, uint8_t* value) {
  // The command byte persists after STOP, so a plain write then read is a register read.
  return chip->bus.write(chip->bus.context, chip->address, &reg, 1) &&
         chip->bus.read(chip->bus.context, chip->address, value, 1);
}
bool xl9555_write(const xl9555_t* chip, uint8_t reg, uint8_t value) {
  const uint8_t bytes[] = {reg, value};
  return chip->bus.write(chip->bus.context, chip->address, bytes, sizeof bytes);
}
bool xl9555_update(const xl9555_t* chip, uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t old, actual;
  if (!xl9555_read(chip, reg, &old)) return false;
  const uint8_t next = (uint8_t)((old & ~mask) | (value & mask));
  return xl9555_write(chip, reg, next) && xl9555_read(chip, reg, &actual) && actual == next;
}
bool xl9555_set_outputs(const xl9555_t* chip, uint8_t port, uint8_t mask, uint8_t value) {
  return xl9555_update(chip, XL9555_OUTPUT0 + port, mask, value) &&
         xl9555_update(chip, XL9555_CONFIG0 + port, mask, 0);
}

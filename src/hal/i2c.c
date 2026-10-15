#include "i2c.h"
#include <esp32-hal-i2c.h>
bool hal_i2c_begin(hal_i2c_t* bus, int sda, int scl, uint32_t frequency) {
  return i2cInit(bus->port, sda, scl, frequency) == ESP_OK;
}
void hal_i2c_end(hal_i2c_t* bus) { i2cDeinit(bus->port); }
static bool write_bytes(void* context, uint8_t address, const uint8_t* data, size_t size) {
  const hal_i2c_t* bus = (const hal_i2c_t*)context;
  return i2cWrite(bus->port, address, data, size, bus->timeout_ms) == ESP_OK;
}
static bool read_bytes(void* context, uint8_t address, uint8_t* data, size_t size) {
  const hal_i2c_t* bus = (const hal_i2c_t*)context;
  size_t count = 0;
  return i2cRead(bus->port, address, data, size, bus->timeout_ms, &count) == ESP_OK && count == size;
}
i2c_bus_t hal_i2c_bus(hal_i2c_t* bus) {
  const i2c_bus_t result = {bus, write_bytes, read_bytes};
  return result;
}

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "drivers/i2c_bus.h"
// Arduino-ESP32 I2C master port. hal_i2c_bus() adapts it to the drivers' i2c_bus_t.
typedef struct {
  uint8_t port;
  uint32_t timeout_ms; // Per transaction.
} hal_i2c_t;
bool hal_i2c_begin(hal_i2c_t* bus, int sda, int scl, uint32_t frequency);
void hal_i2c_end(hal_i2c_t* bus);
i2c_bus_t hal_i2c_bus(hal_i2c_t* bus);

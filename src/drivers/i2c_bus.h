#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Byte transport supplied by the board to every I2C chip driver. Both calls
// return true only when the whole transfer completed; a zero-length write
// probes for an acknowledgement.
typedef struct {
  void* context;
  bool (*write)(void* context, uint8_t address, const uint8_t* data, size_t size);
  bool (*read)(void* context, uint8_t address, uint8_t* data, size_t size);
} i2c_bus_t;

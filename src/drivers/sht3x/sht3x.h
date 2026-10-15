#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "drivers/i2c_bus.h"
// SHT3x humidity/temperature sensor at 0x44, single-shot measurements.
typedef struct { i2c_bus_t bus; } sht3x_t;
// High repeatability, no clock stretching; the result is ready after 15 ms.
bool sht3x_start(const sht3x_t* chip);
// Reads the measurement started at least 15 ms earlier. False on a bus or CRC error.
bool sht3x_fetch(const sht3x_t* chip, int32_t* milli_celsius, uint32_t* milli_percent);
// CRC-8, polynomial 0x31, initial 0xFF.
uint8_t sht3x_crc(const uint8_t* data, size_t size);
// Two CRC-checked words: temperature then humidity.
bool sht3x_decode(const uint8_t data[6], int32_t* milli_celsius, uint32_t* milli_percent);

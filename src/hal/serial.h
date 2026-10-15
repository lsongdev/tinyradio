#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
// The USB serial console. Arduino exposes it only as a C++ object, so this
// thin C interface is implemented in serial.cpp.
void serial_begin(uint32_t baud);
int serial_read(void); // Next received byte, or -1 when none is waiting.
size_t serial_write(const void* data, size_t size);
void serial_print(const char* text);
void serial_println(const char* text);
void serial_printf(const char* format, ...) __attribute__((format(printf, 1, 2)));

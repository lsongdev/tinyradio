#include <Arduino.h>
extern "C" {
#include "serial.h"
}
void serial_begin(uint32_t baud) { Serial.begin(baud); }
int serial_read(void) { return Serial.available() ? Serial.read() : -1; }
size_t serial_write(const void* data, size_t size) { return Serial.write(static_cast<const uint8_t*>(data), size); }
void serial_print(const char* text) { Serial.print(text); }
void serial_println(const char* text) { Serial.println(text); }
void serial_printf(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  const int length = vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  if (length >= int(sizeof buffer)) {
    // Long status lines exceed the stack buffer; format them on the heap.
    if (char* line = static_cast<char*>(malloc(length + 1))) {
      vsnprintf(line, length + 1, format, copy);
      Serial.write(reinterpret_cast<const uint8_t*>(line), length);
      free(line);
    }
  } else if (length > 0) Serial.write(reinterpret_cast<const uint8_t*>(buffer), length);
  va_end(copy);
}

#include "sht3x.h"
enum { ADDRESS = 0x44 };
uint8_t sht3x_crc(const uint8_t* data, size_t size) {
  uint8_t value = 0xFF;
  for (size_t i = 0; i < size; ++i) {
    value ^= data[i];
    for (int bit = 0; bit < 8; ++bit) value = (value & 0x80) ? (uint8_t)((value << 1) ^ 0x31) : (uint8_t)(value << 1);
  }
  return value;
}
bool sht3x_decode(const uint8_t data[6], int32_t* milli_celsius, uint32_t* milli_percent) {
  if (sht3x_crc(data, 2) != data[2] || sht3x_crc(data + 3, 2) != data[5]) return false;
  const uint32_t temperature = (uint32_t)data[0] << 8 | data[1];
  const uint32_t humidity = (uint32_t)data[3] << 8 | data[4];
  // T = -45 + 175 x raw / 65535 degrees C; RH = 100 x raw / 65535 %.
  *milli_celsius = -45000 + (int32_t)((175000ULL * temperature + 32767) / 65535);
  *milli_percent = (uint32_t)((100000ULL * humidity + 32767) / 65535);
  return true;
}
bool sht3x_start(const sht3x_t* chip) {
  static const uint8_t command[] = {0x24, 0x00};
  return chip->bus.write(chip->bus.context, ADDRESS, command, sizeof command);
}
bool sht3x_fetch(const sht3x_t* chip, int32_t* milli_celsius, uint32_t* milli_percent) {
  uint8_t data[6];
  return chip->bus.read(chip->bus.context, ADDRESS, data, sizeof data) && sht3x_decode(data, milli_celsius, milli_percent);
}

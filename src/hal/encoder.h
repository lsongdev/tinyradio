#pragma once
#include <Arduino.h>
namespace hal {
class Encoder {
public:
  void begin();
  int update(); // Drain complete quadrature cycles captured by interrupts.
private:
  static void IRAM_ATTR interrupt(void* context);
  uint8_t previous_ = 0;
  volatile int accumulator_ = 0;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};
}

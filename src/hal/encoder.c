#include "encoder.h"
#include <Arduino.h>
#include <soc/gpio_struct.h>
static DRAM_ATTR const int8_t transitions[16] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
static inline uint32_t IRAM_ATTR level(int pin) {
  return pin < 32 ? (GPIO.in >> pin) & 1 : (GPIO.in1.val >> (pin - 32)) & 1;
}
static void IRAM_ATTR changed(void* context) {
  hal_encoder_t* encoder = (hal_encoder_t*)context;
  const uint8_t current = (level(encoder->a) << 1) | level(encoder->b);
  portENTER_CRITICAL_ISR(&encoder->mux);
  // Reject impossible two-bit transitions; preserve completed cycles.
  if ((encoder->previous ^ current) == 3) encoder->accumulator -= encoder->accumulator % 4;
  else encoder->accumulator += transitions[(encoder->previous << 2) | current];
  encoder->previous = current;
  portEXIT_CRITICAL_ISR(&encoder->mux);
}
void hal_encoder_begin(hal_encoder_t* encoder, int pin_a, int pin_b, bool pull_up) {
  const portMUX_TYPE unlocked = portMUX_INITIALIZER_UNLOCKED;
  encoder->a = pin_a; encoder->b = pin_b; encoder->accumulator = 0; encoder->mux = unlocked;
  pinMode(pin_a, pull_up ? INPUT_PULLUP : INPUT); pinMode(pin_b, pull_up ? INPUT_PULLUP : INPUT);
  encoder->previous = (digitalRead(pin_a) << 1) | digitalRead(pin_b);
  attachInterruptArg(pin_a, changed, encoder, CHANGE);
  attachInterruptArg(pin_b, changed, encoder, CHANGE);
}
int hal_encoder_read(hal_encoder_t* encoder) {
  portENTER_CRITICAL(&encoder->mux);
  const int turns = encoder->accumulator / 4;
  encoder->accumulator -= turns * 4;
  portEXIT_CRITICAL(&encoder->mux);
  return turns;
}

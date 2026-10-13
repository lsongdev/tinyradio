#include "encoder.h"
#include "board.h"
#include <soc/gpio_struct.h>
namespace hal {
namespace {
DRAM_ATTR int8_t transitions[16] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
}
void Encoder::begin() {
  pinMode(board::encoderA, INPUT_PULLUP); pinMode(board::encoderB, INPUT_PULLUP);
  previous_ = (digitalRead(board::encoderA) << 1) | digitalRead(board::encoderB);
  attachInterruptArg(board::encoderA, interrupt, this, CHANGE);
  attachInterruptArg(board::encoderB, interrupt, this, CHANGE);
}
void IRAM_ATTR Encoder::interrupt(void* context) {
  auto& encoder = *static_cast<Encoder*>(context);
  const uint32_t pins = GPIO.in;
  const uint8_t current = (((pins >> board::encoderA) & 1) << 1) | ((pins >> board::encoderB) & 1);
  portENTER_CRITICAL_ISR(&encoder.mux_);
  // Reject impossible two-bit transitions; preserve completed cycles.
  if ((encoder.previous_ ^ current) == 3) encoder.accumulator_ -= encoder.accumulator_ % 4;
  else encoder.accumulator_ += transitions[(encoder.previous_ << 2) | current];
  encoder.previous_ = current;
  portEXIT_CRITICAL_ISR(&encoder.mux_);
}
int Encoder::update() {
  portENTER_CRITICAL(&mux_);
  const int turns = accumulator_ / 4;
  accumulator_ -= turns * 4;
  portEXIT_CRITICAL(&mux_);
  return turns;
}
}

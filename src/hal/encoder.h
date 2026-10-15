#pragma once
#include <freertos/FreeRTOS.h>
#include <stdbool.h>
#include <stdint.h>
// Quadrature encoder decoded in GPIO interrupts; four transitions per detent.
typedef struct {
  int a, b;
  uint8_t previous;
  volatile int accumulator;
  portMUX_TYPE mux;
} hal_encoder_t;
// pull_up = false where the board provides pull-ups (always on GPIO34-39).
void hal_encoder_begin(hal_encoder_t* encoder, int pin_a, int pin_b, bool pull_up);
// Drain complete quadrature cycles captured by interrupts.
int hal_encoder_read(hal_encoder_t* encoder);

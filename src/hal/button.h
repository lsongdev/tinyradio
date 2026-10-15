#pragma once
#include <stdbool.h>
#include <stdint.h>
// Active-low push button with a 25 ms debounce. GPIO34-39 have no internal
// pull-up; pass pull_up = false where the board provides one.
typedef struct {
  int pin;
  bool raw, pressed;
  uint32_t changed;
} hal_button_t;
void hal_button_begin(hal_button_t* button, int pin, bool pull_up);
// Call frequently. Returns true once for each debounced press.
bool hal_button_update(hal_button_t* button, uint32_t now);

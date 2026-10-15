#pragma once
#include <stdbool.h>
#include <stdint.h>
// Active-low push button with a 25 ms debounce. GPIO34-39 have no internal
// pull-up; pass pull_up = false where the board provides one.
typedef struct {
  int pin;
  bool raw, pressed;
  bool repeating;      // Pressed after begin(); a key held since begin() never repeats.
  uint32_t changed, next_repeat;
} hal_button_t;
void hal_button_begin(hal_button_t* button, int pin, bool pull_up);
// Call frequently. Returns true once for each debounced press.
bool hal_button_update(hal_button_t* button, uint32_t now);
// While a press is held: true once `delay` ms after it, then every `interval` ms.
bool hal_button_repeat(hal_button_t* button, uint32_t now, uint32_t delay, uint32_t interval);

#include "button.h"
#include <Arduino.h>
void hal_button_begin(hal_button_t* button, int pin, bool pull_up) {
  button->pin = pin;
  pinMode(pin, pull_up ? INPUT_PULLUP : INPUT);
  button->raw = button->pressed = digitalRead(pin) == LOW;
  button->changed = millis();
}
bool hal_button_update(hal_button_t* button, uint32_t now) {
  const bool pressed = digitalRead(button->pin) == LOW;
  if (pressed != button->raw) { button->raw = pressed; button->changed = now; }
  if (now - button->changed < 25 || button->pressed == button->raw) return false;
  button->pressed = button->raw;
  return button->pressed;
}

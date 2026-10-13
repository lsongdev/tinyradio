#include "button.h"
#include <Arduino.h>
namespace hal {
void Button::begin() {
  pinMode(pin_, INPUT_PULLUP);
  raw_ = stable_ = digitalRead(pin_) == LOW;
  changed_ = millis();
}
bool Button::update(uint32_t now) {
  const bool pressed = digitalRead(pin_) == LOW;
  if (pressed != raw_) { raw_ = pressed; changed_ = now; }
  if (now - changed_ < 25 || stable_ == raw_) return false;
  stable_ = raw_;
  return stable_;
}
}

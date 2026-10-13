#pragma once
#include <stdint.h>
namespace hal {
class Button {
public:
  explicit Button(int pin) : pin_(pin) {}
  void begin();
  // Call frequently. Returns true once for each debounced press.
  bool update(uint32_t now);
  bool pressed() const { return stable_; }
private:
  int pin_;
  bool raw_ = false, stable_ = false;
  uint32_t changed_ = 0;
};
}

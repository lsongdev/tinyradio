#pragma once
#include <Wire.h>
namespace hal {
class RadioHardware {
public:
  void begin();
  void setPowered(bool powered);
  void reset();
  TwoWire& bus() { return Wire; }
private:
  bool powered_ = false;
};
}

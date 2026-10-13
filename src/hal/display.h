#pragma once
#include <LovyanGFX.hpp>
namespace hal {
class Display : public lgfx::LGFX_Device {
public:
  Display();
  bool begin();
  void setBrightness(uint8_t value);
private:
  lgfx::Bus_Parallel8 bus_;
  lgfx::Panel_ST7789 panel_;
};
}

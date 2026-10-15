#include "boards/display.hpp"
#include <Arduino.h>
extern "C" {
#include "board.h"
#include "pins.h"
}
namespace {
// ST7789 170x320 on an 8-bit parallel bus. LovyanGFX is C++, so the panel
// configuration is the one board part outside the C layer.
class Panel : public lgfx::LGFX_Device {
public:
  Panel() {
    auto bus = bus_.config();
    bus.freq_write = 10000000;
    bus.pin_wr = PIN_LCD_WR; bus.pin_rd = PIN_LCD_RD; bus.pin_rs = PIN_LCD_DC;
    bus.pin_d0 = PIN_LCD_D0; bus.pin_d1 = PIN_LCD_D1;
    bus.pin_d2 = PIN_LCD_D2; bus.pin_d3 = PIN_LCD_D3;
    bus.pin_d4 = PIN_LCD_D4; bus.pin_d5 = PIN_LCD_D5;
    bus.pin_d6 = PIN_LCD_D6; bus.pin_d7 = PIN_LCD_D7;
    bus_.config(bus);
    panel_.setBus(&bus_);
    auto panel = panel_.config();
    panel.pin_cs = PIN_LCD_CS; panel.pin_rst = PIN_LCD_RESET;
    panel.panel_width = 170; panel.panel_height = 320;
    panel.memory_width = 240; panel.memory_height = 320;
    panel.offset_x = 35; panel.offset_y = 0;
    panel.invert = true; panel.rgb_order = false;
    panel.bus_shared = false;
    panel_.config(panel);
    setPanel(&panel_);
  }
private:
  lgfx::Bus_Parallel8 bus_;
  lgfx::Panel_ST7789 panel_;
};
Panel panel;
}
lgfx::LGFX_Device& board_display_device() { return panel; }
bool board_display_begin(void) {
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, LOW);
  if (!panel.init()) return false;
  panel.setRotation(3); // 320 x 170 landscape; confirmed orientation on user's unit
  panel.fillScreen(TFT_BLACK);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BACKLIGHT, 0);
  board_display_set_brightness(128);
  return true;
}
void board_display_set_brightness(uint8_t value) { ledcWrite(0, value); }

#include "display.h"
#include "board.h"
#include <Arduino.h>
namespace hal {
Display::Display() {
  auto bus = bus_.config();
  bus.freq_write = 10000000;
  bus.pin_wr = board::lcdWr; bus.pin_rd = board::lcdRd; bus.pin_rs = board::lcdDc;
  bus.pin_d0 = board::lcdData[0]; bus.pin_d1 = board::lcdData[1];
  bus.pin_d2 = board::lcdData[2]; bus.pin_d3 = board::lcdData[3];
  bus.pin_d4 = board::lcdData[4]; bus.pin_d5 = board::lcdData[5];
  bus.pin_d6 = board::lcdData[6]; bus.pin_d7 = board::lcdData[7];
  bus_.config(bus);
  panel_.setBus(&bus_);
  auto panel = panel_.config();
  panel.pin_cs = board::lcdCs; panel.pin_rst = board::lcdReset;
  panel.panel_width = 170; panel.panel_height = 320;
  panel.memory_width = 240; panel.memory_height = 320;
  panel.offset_x = 35; panel.offset_y = 0;
  panel.invert = true; panel.rgb_order = false;
  panel.bus_shared = false;
  panel_.config(panel);
  setPanel(&panel_);
}
bool Display::begin() {
  pinMode(board::backlight, OUTPUT);
  digitalWrite(board::backlight, LOW);
  if (!init()) return false;
  setRotation(3); // 320 x 170 landscape; confirmed orientation on user's unit
  fillScreen(TFT_BLACK);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(board::backlight, 0);
  setBrightness(128);
  return true;
}
void Display::setBrightness(uint8_t value) { ledcWrite(0, value); }
}

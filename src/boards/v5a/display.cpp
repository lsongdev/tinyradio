#include "boards/display.hpp"
#include <Arduino.h>
#include <driver/gpio.h>
extern "C" {
#include "board.h"
#include "pins.h"
}
namespace {
// ST7789 240x320 on VSPI, write-only. VSPI avoids HSPI, which the SDK uses
// for the PSRAM clock.
class Panel : public lgfx::LGFX_Device {
public:
  Panel() {
    auto bus = bus_.config();
    bus.spi_host = VSPI_HOST;
    bus.spi_mode = 0;
    bus.freq_write = 40000000;
    bus.spi_3wire = true;
    bus.use_lock = true;
    bus.dma_channel = SPI_DMA_CH_AUTO;
    bus.pin_sclk = PIN_TFT_SCLK; bus.pin_mosi = PIN_TFT_MOSI; bus.pin_miso = -1; bus.pin_dc = PIN_TFT_DC;
    bus_.config(bus);
    panel_.setBus(&bus_);
    auto panel = panel_.config();
    panel.pin_cs = PIN_TFT_CS; panel.pin_rst = -1; panel.pin_busy = -1;
    panel.panel_width = 240; panel.panel_height = 320;
    panel.memory_width = 240; panel.memory_height = 320;
    panel.offset_x = 0; panel.offset_y = 0;
    panel.invert = true; panel.rgb_order = false;
    panel.readable = false; panel.bus_shared = false;
    panel_.config(panel);
    setPanel(&panel_);
  }
private:
  lgfx::Bus_SPI bus_;
  lgfx::Panel_ST7789 panel_;
};
Panel panel;
bool ready = false;
}
lgfx::LGFX_Device& board_display_device() { return panel; }
bool board_display_begin(void) {
  gpio_hold_dis(static_cast<gpio_num_t>(PIN_BACKLIGHT)); // Held low through the last deep sleep.
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, LOW);
  if (!panel.init()) return false;
  ready = true;
  panel.setRotation(1); // 320 x 240 landscape; confirmed upright on the V5A.
  panel.fillScreen(TFT_BLACK);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BACKLIGHT, 0);
  board_display_set_brightness(128);
  // GPIO12 is pulled-up key C; restore its pull-up after SPI setup, as the
  // original firmware does.
  pinMode(PIN_KEY_C, INPUT_PULLUP);
  return true;
}
void board_display_set_brightness(uint8_t value) { ledcWrite(0, value); }
void board_display_sleep(void) {
  if (ready) panel.sleep();
  ledcDetachPin(PIN_BACKLIGHT);
  digitalWrite(PIN_BACKLIGHT, LOW);
  pinMode(PIN_BACKLIGHT, OUTPUT);
  gpio_hold_en(static_cast<gpio_num_t>(PIN_BACKLIGHT));
  gpio_deep_sleep_hold_en();
}

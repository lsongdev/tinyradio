#pragma once
// Records rendering operations so host tests can inspect real UI color choices.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace render_test {
enum Surface { PANEL, FRAME, EXTRAS };
struct Draw { Surface surface; std::string operation; int x, y; uint16_t color, background; std::string text; };
extern std::vector<Draw> draws;
extern bool frame_available, extras_available;
}
namespace lgfx {
enum class textdatum_t { top_center, top_right, top_left, middle_center, middle_left, baseline_left };
struct FontMetrics { int height = 24, baseline = 20; };
struct TestFont { void getDefaultMetric(FontMetrics* metrics) const { *metrics = {}; } };
struct bgr888_t { uint8_t b = 0, g = 0, r = 0; };
class LGFXBase {
public:
  render_test::Surface surface = render_test::PANEL;
  uint16_t ink = 0, background = 0;
  int cursor_x = 0, cursor_y = 0;
  void record(const char* operation, int x, int y, uint16_t color, const char* text = "") {
    render_test::draws.push_back({surface, operation, x, y, color, background, text});
  }
  void drawCircle(int x, int y, int, uint16_t c) { record("circle", x, y, c); }
  void drawLine(int x, int y, int, int, uint16_t c) { record("line", x, y, c); }
  void drawRect(int x, int y, int, int, uint16_t c) { record("rect", x, y, c); }
  void fillRect(int x, int y, int, int, uint16_t c) { record("fill", x, y, c); }
  void drawFastVLine(int x, int y, int, uint16_t c) { record("vline", x, y, c); }
  void drawFastHLine(int x, int y, int, uint16_t c) { record("hline", x, y, c); }
  void fillTriangle(int x, int y, int, int, int, int, uint16_t c) { record("triangle", x, y, c); }
  void fillScreen(uint16_t c) { record("screen", 0, 0, c); }
  void setTextColor(uint16_t c, uint16_t bg) { ink = c; background = bg; }
  void setFont(const TestFont*) {}
  void setTextSize(float) {}
  void setTextDatum(textdatum_t) {}
  int textWidth(const char* text) { return int(strlen(text)) * 6; }
  int fontHeight() { return 24; }
  void setClipRect(int, int, int, int) {}
  void clearClipRect() {}
  void drawString(const char* text, int x, int y) { record("text", x, y, ink, text); }
  void setCursor(int x, int y) { cursor_x = x; cursor_y = y; }
  void print(const char* text) { drawString(text, cursor_x, cursor_y); }
  template <typename... Args> void printf(const char* format, Args... args) {
    char text[128]; snprintf(text, sizeof text, format, args...); print(text);
  }
  void readRectRGB(int, int, int width, int, bgr888_t* pixels) {
    for (int i = 0; i < width; ++i) pixels[i] = {};
  }
};
class LGFX_Device : public LGFXBase {};
class LGFX_Sprite : public LGFXBase {
public:
  explicit LGFX_Sprite(LGFX_Device*) {}
  void setColorDepth(int) {}
  void* createSprite(int, int height) {
    surface = height == 170 ? render_test::FRAME : render_test::EXTRAS;
    return (surface == render_test::FRAME ? render_test::frame_available : render_test::extras_available) ? this : nullptr;
  }
  void pushSprite(int x, int y) { record("push", x, y, 0); }
};
}
namespace fonts { inline lgfx::TestFont Font0, efontCN_24_b; }
lgfx::LGFX_Device& board_display_device();

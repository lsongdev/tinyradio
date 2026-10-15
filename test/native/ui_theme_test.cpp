#include "boards/display.hpp"
extern "C" {
#include "ui/ui.h"
#include "hal/serial.h"
#include "services/spectrum.h"
}
#include <cassert>
#include <cmath>

namespace render_test {
std::vector<Draw> draws;
bool frame_available = true, extras_available = true;
}
lgfx::LGFX_Device& board_display_device() { static lgfx::LGFX_Device device; return device; }
void serial_print(const char*) {}
void serial_println(const char*) {}
void serial_printf(const char*, ...) {}
size_t serial_write(const void*, size_t size) { return size; }

using namespace render_test;
static const Draw& find(Surface surface, const char* operation, int x, int y) {
  for (const auto& draw : draws)
    if (draw.surface == surface && draw.operation == operation && draw.x == x && draw.y == y) return draw;
  assert(false && "Missing draw operation");
  return draws.front();
}
static void checkSignalRow(Surface surface, bool fm) {
  const auto& sm = find(surface, "rect", 258, 70);
  const auto& step = find(surface, "rect", 258, 91);
  assert(step.width == sm.width && step.height == sm.height);
  int previousRight = 35, count = 0;
  for (const auto& draw : draws) {
    if (draw.surface != surface || draw.operation != "text" || draw.y != 95) continue;
    assert(draw.x > previousRight); // Readings must also be separated from the bars.
    previousRight = draw.x + board_display_device().textWidth(draw.text.c_str());
    assert(previousRight <= 252); // Clear the Step control and its six-pixel gap.
    ++count;
  }
  assert(count == (fm ? 6 : 5) && previousRight == 252);
}
static double luminance(uint16_t rgb) {
  const int red = (rgb >> 11) & 31, green = (rgb >> 5) & 63, blue = rgb & 31;
  const auto linear = [](int channel) {
    const double value = channel / 255.0;
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
  };
  return .2126 * linear((red << 3) | (red >> 2)) + .7152 * linear((green << 2) | (green >> 4)) +
         .0722 * linear((blue << 3) | (blue >> 2));
}
int main() {
  const auto& light = *ui_theme_get(UI_THEME_LIGHT);
  for (auto color : {light.text_primary, light.text_secondary, light.text_muted, light.control_disabled,
                     light.accent, light.control_focus, light.status_positive, light.status_danger,
                     light.status_notice, light.audio_muted, light.audio_stereo, light.active,
                     light.ruler_current, light.ruler_adjacent})
    assert((luminance(light.background) + .05) / (luminance(color) + .05) >= 4.5);
  radio_model_t model; radio_model_init(&model);
  uint8_t bars[SPECTRUM_BARS]; memset(bars, 128, sizeof bars);
  ui_view_t view{};
  view.model = &model; view.field = UI_FIELD_SOFT_MUTE;
  view.volume = 18; view.brightness = 128;
  view.ps = ""; view.message = ""; view.error = "test error";
  view.date_time = "2026-10-15 12:34:56"; view.spectrum = bars;
  view.supply.plausible = view.supply.external_power_likely = true;
  view.audio_status_known = view.signal.quality_known = view.signal.valid = true;
  view.signal.stereo = view.signal.soft_muted = true;
  view.reception.soft_mute_db = 16; view.band_locked = view.seek_mode = true; view.seek_arrow = 1;

  // Theme changes must repaint the extras even with identical telemetry and bars.
  for (const auto& buffers : {std::pair<bool, bool>{true, true}, {true, false}, {false, false}}) {
    const bool buffered = buffers.first;
    frame_available = buffered; extras_available = buffers.second;
    draws.clear();
    assert(ui_begin(UI_THEME_LIGHT) == buffered);
    assert(find(PANEL, "screen", 0, 0).color == ui_theme_get(UI_THEME_LIGHT)->background);
    const auto main = buffered ? FRAME : PANEL, extra = extras_available ? EXTRAS : PANEL;
    const int extra_top = extras_available ? 0 : 170;
    for (auto id : {UI_THEME_DARK, UI_THEME_LIGHT, UI_THEME_DARK}) {
      view.theme = id;
      const auto& theme = *ui_theme_get(id);
      draws.clear(); ui_draw(&view);
      assert(find(main, "screen", 0, 0).color == theme.background);
      assert(find(main, "text", 100, 10).color == theme.active); // Locked VHF.
      assert(find(main, "rect", 274, 6).color == theme.active); // External power.
      assert(find(main, "triangle", 303, 39).color == theme.active); // Seek arrow.
      assert(find(main, "text", 287, 78).color == theme.active); // Soft mute text.
      assert(find(main, "rect", 258, 70).color == theme.control_focus); // Focus survives.
      assert(find(main, "rect", 3, 2).color == theme.text_secondary); // Unselected band stays gray.
      assert(find(main, "text", 16, 10).color == theme.text_secondary);
      assert(find(main, "rect", 87, 2).color == theme.accent); // Selected band.
      assert(find(main, "rect", 3, 70).color == theme.accent); // Selected FM mode.
      assert(find(main, "rect", 31, 70).color == theme.control_disabled); // AM unavailable.
      assert(find(main, "rect", 117, 70).color == theme.accent); // BW control.
      assert(find(main, "rect", 258, 91).color == theme.accent); // Step aligns with SM.
      assert(find(extra, "fill", 12, extra_top + 24).color == theme.accent); // Spectrum bar.
      assert(find(main, "dot", 239, 10).color == theme.audio_stereo);
      checkSignalRow(main, true);
      assert(find(extra, "fill", 0, extra_top).color == theme.background);
      for (const auto& draw : draws) {
        if (draw.operation == "text") {
          assert(draw.background == theme.background);
          assert(draw.text != "AIR"); // Unsupported bands are hidden, not grayed out.
        }
        assert(!(draw.surface == main && draw.operation == "rect" && draw.x == 115 && draw.y == 2));
      }
      draws.clear(); ui_draw_extras(&view);
      assert(draws.empty()); // The unchanged extras are still cached within a theme.
    }
    view.muted = true; view.field = UI_FIELD_VOLUME;
    draws.clear(); ui_draw(&view);
    assert(find(main, "rect", 232, 8).color == ui_theme_get(view.theme)->audio_muted);
    assert(find(main, "text", 248, 7).color == ui_theme_get(view.theme)->control_focus);
    view.muted = false; view.field = UI_FIELD_SOFT_MUTE;
  }
  radio_set_band_available(RADIO_BAND_AIR, true); // The V5A enables AIR during startup.
  draws.clear(); ui_draw(&view);
  assert(find(PANEL, "rect", 115, 2).color == ui_theme_get(view.theme)->text_secondary);
  assert(find(PANEL, "text", 128, 10).text == "AIR");
  radio_set_band_available(RADIO_BAND_AIR, false);
  // Keep all readings visible at their longest values, during transitions, and without FM's pilot.
  view.signal.rssi = view.signal.snr = view.signal.gain_index = 255;
  view.signal.valid = false;
  for (auto id : {UI_THEME_DARK, UI_THEME_LIGHT}) {
    view.theme = id;
    for (size_t band : {size_t(3), size_t(1)}) {
      assert(radio_model_set_band(&model, band));
      for (int state = 0; state < 3; ++state) {
        view.pending = state == 1; view.failed = state == 2;
        draws.clear(); ui_draw(&view);
        checkSignalRow(PANEL, radio_model_band(&model)->fm);
      }
    }
  }
  puts("UI themes: shared brand accent and active colors, state overrides, backgrounds and extras cache passed");
}

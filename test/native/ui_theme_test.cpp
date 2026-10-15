#include "boards/display.hpp"
extern "C" {
#include "ui/ui.h"
#include "hal/serial.h"
#include "services/spectrum.h"
}
#include <cassert>

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
int main() {
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
  for (const auto buffers : {std::pair<bool, bool>{true, true}, {true, false}, {false, false}}) {
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
      assert(find(main, "rect", 115, 2).color == theme.control_disabled); // AIR unavailable.
      assert(find(main, "rect", 3, 70).color == theme.accent); // Selected FM mode.
      assert(find(main, "rect", 31, 70).color == theme.control_disabled); // AM unavailable.
      assert(find(main, "rect", 117, 70).color == theme.accent); // BW control.
      assert(find(main, "rect", 278, 91).color == theme.accent); // Step control.
      assert(find(extra, "fill", 12, extra_top + 24).color == theme.accent); // Spectrum bar.
      assert(find(main, "rect", 232, 8).color == theme.audio_stereo);
      assert(find(extra, "fill", 0, extra_top).color == theme.background);
      for (const auto& draw : draws)
        if (draw.operation == "text") assert(draw.background == theme.background);
      draws.clear(); ui_draw_extras(&view);
      assert(draws.empty()); // The unchanged extras are still cached within a theme.
    }
    view.muted = true; view.field = UI_FIELD_VOLUME;
    draws.clear(); ui_draw(&view);
    assert(find(main, "rect", 232, 8).color == ui_theme_get(view.theme)->audio_muted);
    assert(find(main, "text", 248, 7).color == ui_theme_get(view.theme)->control_focus);
    view.muted = false; view.field = UI_FIELD_SOFT_MUTE;
  }
  puts("UI themes: shared brand accent and active colors, state overrides, backgrounds and extras cache passed");
}

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
static const Draw* signalText(const char* text) {
  for (const auto& draw : draws)
    if (draw.surface == PANEL && draw.operation == "text" && draw.y == 95 && draw.text == text) return &draw;
  return nullptr;
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
  assert(count == (fm ? 5 : 4) && previousRight == 252);
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
  // Measure the actual RGB565 values, for both light and dark backgrounds.
  for (int i = 0; i < UI_THEME_COUNT; ++i) {
    const auto& theme = *ui_theme_get((ui_theme_id_t)i);
    const auto contrast = [&](uint16_t color) {
      const double a = luminance(theme.background), b = luminance(color);
      return (std::fmax(a, b) + .05) / (std::fmin(a, b) + .05);
    };
    for (auto color : {theme.text_primary, theme.text_secondary, theme.text_muted, theme.control_disabled,
                       theme.accent, theme.control_focus, theme.status_positive, theme.status_danger,
                       theme.status_notice, theme.audio_muted, theme.active,
                       theme.ruler_current, theme.ruler_adjacent})
      assert(contrast(color) >= 4.5);
    assert(contrast(theme.text_primary) >= 12);
    assert(contrast(theme.text_secondary) >= 7);
    assert(contrast(theme.border_subtle) >= 3);
    assert(theme.accent != theme.control_focus && theme.accent != theme.active && theme.control_focus != theme.active);
    assert(ui_theme_name((ui_theme_id_t)i)[0]);
  }
  assert(ui_theme_get((ui_theme_id_t)UI_THEME_COUNT) == ui_theme_get(UI_THEME_DARK));
  assert(ui_theme_get((ui_theme_id_t)-1) == ui_theme_get(UI_THEME_DARK));
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
    for (int index = 0; index <= UI_THEME_COUNT; ++index) {
      const auto id = (ui_theme_id_t)(index % UI_THEME_COUNT);
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
      assert(find(main, "dot", 239, 10).color == theme.text_secondary);
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
    assert(find(main, "rect", 232, 8).color == ui_theme_get(view.theme)->control_focus);
    assert(find(main, "text", 248, 7).color == ui_theme_get(view.theme)->control_focus);
    view.muted = false; view.field = UI_FIELD_SOFT_MUTE;
  }
  radio_set_band_available(RADIO_BAND_AIR, true); // The V5A enables AIR during startup.
  draws.clear(); ui_draw(&view);
  assert(find(PANEL, "rect", 115, 2).color == ui_theme_get(view.theme)->text_secondary);
  assert(find(PANEL, "text", 128, 10).text == "AIR");
  radio_set_band_available(RADIO_BAND_AIR, false);
  // Keep all readings visible at their longest values, during transitions, and without FM's RDS label.
  view.signal.rssi = view.signal.snr = view.signal.gain_index = 255;
  view.signal.valid = false;
  for (int index = 0; index < UI_THEME_COUNT; ++index) {
    const auto id = (ui_theme_id_t)index;
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
  // RDS reflects live synchronization, not retained text or the last signal-quality poll.
  for (int index = 0; index < UI_THEME_COUNT; ++index) {
    const auto id = (ui_theme_id_t)index;
    view.theme = id;
    const auto& theme = *ui_theme_get(id);
    assert(radio_model_set_band(&model, 3));
    view.failed = view.pending = false;
    view.signal.quality_known = true;
    view.rds_synchronized = true;
    view.ps = "";
    draws.clear(); ui_draw(&view);
    assert(signalText("RDS") && signalText("RDS")->color == theme.active);
    assert(signalText("SNR255dB") && !signalText("RF255") && !signalText("P1"));
    view.signal.quality_known = false;
    draws.clear(); ui_draw(&view);
    assert(signalText("RDS")->color == theme.active && signalText("SNR--dB"));
    view.ps = "OLD NAME"; view.rds_synchronized = false;
    draws.clear(); ui_draw(&view);
    assert(signalText("RDS")->color == theme.text_muted);
    view.rds_synchronized = true; view.pending = true;
    draws.clear(); ui_draw(&view);
    assert(signalText("RDS")->color == theme.text_muted);
    view.pending = false; view.failed = true;
    draws.clear(); ui_draw(&view);
    assert(signalText("RDS")->color == theme.text_muted);
    view.failed = false;
    assert(radio_model_set_band(&model, 1));
    draws.clear(); ui_draw(&view);
    assert(!signalText("RDS"));
    view.ps = "";
  }
  // Configuration, runtime emphasis and focus are independent control states.
  for (int index = 0; index < UI_THEME_COUNT; ++index) {
    const auto id = (ui_theme_id_t)index;
    view.theme = id;
    const auto& theme = *ui_theme_get(id);
    view.failed = view.pending = false;
    view.signal.quality_known = true;
    view.field = UI_FIELD_FREQUENCY;
    const auto checkSm = [&](uint16_t border, uint16_t text) {
      draws.clear(); ui_draw(&view);
      assert(find(PANEL, "rect", 258, 70).color == border);
      assert(find(PANEL, "text", 287, 78).color == text);
    };
    view.reception.soft_mute_db = 0; view.signal.soft_muted = true;
    checkSm(theme.text_secondary, theme.text_secondary); // Off ignores stale chip attenuation.
    view.field = UI_FIELD_SOFT_MUTE;
    checkSm(theme.control_focus, theme.control_focus); // Off remains adjustable.
    view.reception.soft_mute_db = 16; view.signal.soft_muted = false;
    view.field = UI_FIELD_FREQUENCY;
    checkSm(theme.accent, theme.accent); // Configured, but currently idle.
    view.signal.soft_muted = true;
    checkSm(theme.accent, theme.active);
    view.field = UI_FIELD_SOFT_MUTE;
    checkSm(theme.control_focus, theme.active); // Runtime emphasis overrides only text.
    view.pending = true;
    checkSm(theme.control_focus, theme.control_focus); // Tuning makes old signal data inapplicable.
    view.pending = false; view.signal.quality_known = false;
    checkSm(theme.control_focus, theme.control_focus);
    view.signal.quality_known = true;

    assert(radio_model_set_band(&model, 1) && radio_model_set_mode(&model, SI473X_AM));
    view.field = UI_FIELD_MODE;
    draws.clear(); ui_draw(&view);
    assert(find(PANEL, "rect", 3, 70).color == theme.control_disabled); // FM not allowed on MW.
    assert(find(PANEL, "rect", 31, 70).color == theme.control_focus);
    assert(find(PANEL, "text", 44, 78).color == theme.control_focus);
    assert(find(PANEL, "rect", 59, 70).color == theme.text_secondary); // LSB available, not selected.
    view.field = UI_FIELD_BAND;
    draws.clear(); ui_draw(&view);
    assert(find(PANEL, "rect", 31, 2).color == theme.control_focus);
    assert(find(PANEL, "text", 44, 10).color == theme.active); // Lock and focus coexist.

    for (int state = 0; state < 4; ++state) {
      view.muted = state == 2; view.audio_status_known = state != 3; view.signal.stereo = state == 1;
      const char* shape = state == 1 ? "dot" : "rect";
      const int x = state == 1 ? 239 : 232, y = state == 1 ? 10 : 8;
      view.field = UI_FIELD_VOLUME;
      draws.clear(); ui_draw(&view);
      assert(find(PANEL, shape, x, y).color == theme.control_focus);
      assert(find(PANEL, "text", 248, 7).color == theme.control_focus);
      view.field = UI_FIELD_FREQUENCY;
      draws.clear(); ui_draw(&view);
      const uint16_t expected = state == 2 ? theme.audio_muted : state == 3 ? theme.text_muted : theme.text_secondary;
      assert(find(PANEL, shape, x, y).color == expected);

      view.supply.plausible = state != 2; view.supply.external_power_likely = state == 3;
      view.supply.estimated_percent = state == 1 ? 10 : 80;
      view.field = UI_FIELD_BATTERY;
      draws.clear(); ui_draw(&view);
      assert(find(PANEL, "rect", 274, 6).color == theme.control_focus);
      assert(find(PANEL, "text", 290, 7).color == theme.control_focus);
      view.field = UI_FIELD_FREQUENCY;
      draws.clear(); ui_draw(&view);
      const uint16_t battery = state == 1 || state == 2 ? theme.status_danger : state == 3 ? theme.active : theme.text_secondary;
      assert(find(PANEL, "rect", 274, 6).color == battery);
      assert(find(PANEL, "text", 290, 7).color == battery);
    }
  }
  puts("UI themes: shared brand accent and active colors, state overrides, backgrounds and extras cache passed");
}

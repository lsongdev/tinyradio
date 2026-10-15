// LovyanGFX is C++; this file is the UI's only C++ and exposes the C API in ui.h.
#include "boards/display.hpp"
#include <Arduino.h>
#include <stdlib.h>
extern "C" {
#include "ui.h"
#include "boards/board.h"
#include "hal/serial.h"
#include "radio/frequency.h"
#include "services/spectrum.h"
}
namespace {
struct ScrollState { char text[129]; int width; uint32_t started; };
lgfx::LGFX_Sprite frame(&board_display_device()), extras(&board_display_device());
bool buffered = false, extras_buffered = false;
char extras_shown[64];
uint8_t spectrum_shown[SPECTRUM_BARS];
bool spectrum_drawn = false;
ScrollState scroll[2];

void drawSun(lgfx::LGFXBase& g, int x, uint16_t color) {
  g.drawCircle(x + 6, 10, 3, color);
  g.drawLine(x + 6, 3, x + 6, 5, color); g.drawLine(x + 6, 15, x + 6, 17, color);
  g.drawLine(x - 1, 10, x + 1, 10, color); g.drawLine(x + 11, 10, x + 13, 10, color);
  g.drawLine(x + 1, 5, x + 2, 6, color); g.drawLine(x + 10, 14, x + 11, 15, color);
  g.drawLine(x + 1, 15, x + 2, 14, color); g.drawLine(x + 10, 6, x + 11, 5, color);
}
void drawSpeaker(lgfx::LGFXBase& g, int x, uint16_t color, bool muted, int volume) {
  g.drawRect(x, 8, 4, 5, color);
  g.drawLine(x + 4, 8, x + 8, 5, color); g.drawLine(x + 8, 5, x + 8, 15, color);
  g.drawLine(x + 8, 15, x + 4, 12, color);
  if (muted) { g.drawLine(x + 10, 7, x + 14, 13, color); g.drawLine(x + 14, 7, x + 10, 13, color); }
  else {
    const int waves = volume == 0 ? 0 : (volume + 20) / 21;
    for (int i = 0; i < waves; ++i) g.drawFastVLine(x + 10 + 2 * i, 9 - i, 3 + 2 * i, color);
  }
}
void drawRuler(lgfx::LGFXBase& g, const radio_model_t& model, bool locked) {
  struct Tick { uint32_t hz, step; size_t band; int x; bool valid; const char* name; bool endpoint; };
  Tick ticks[27]{};
  constexpr int center = 13, spacing = 12;
  ticks[center] = {radio_model_frequency(&model), radio_model_step(&model), model.band, 160, true, nullptr, false};
  // Names occupy ruler divisions, while tuning still crosses bands in one turn.
  for (int direction : {-1, 1}) {
    radio_model_t preview = model;
    int index = center;
    while (index + direction >= 0 && index + direction < 27) {
      radio_model_t next = preview;
      if (locked) radio_model_turn_frequency_in_band(&next, direction);
      else radio_model_turn_frequency(&next, direction);
      if (next.band == preview.band && radio_model_frequency(&next) == radio_model_frequency(&preview)) break;
      if (next.band != preview.band) {
        ticks[index].endpoint = true;
        for (int division = 1; division <= 10; ++division) {
          index += direction;
          if (index < 0 || index >= 27) break;
          const bool departing = division <= 5;
          const radio_model_t& side = departing ? preview : next;
          const char* name = (division == 5 || division == 6) ? radio_model_band(&side)->name : nullptr;
          ticks[index] = {0, radio_model_step(&side), side.band, 160 + (index - center) * spacing, true, name, false};
        }
        if (index + direction < 0 || index + direction >= 27) break;
      }
      index += direction;
      ticks[index] = {radio_model_frequency(&next), radio_model_step(&next), next.band, 160 + (index - center) * spacing,
                      true, nullptr, next.band != preview.band};
      preview = next;
    }
  }
  const auto frequency = radio_format_frequency(radio_model_frequency(&model));
  char current[24]; snprintf(current, sizeof current, "%s%s", frequency.number, frequency.unit);
  g.setTextSize(1); g.setTextDatum(lgfx::textdatum_t::top_center);
  const int currentWidth = g.textWidth(current);
  g.drawFastHLine(0, 140, 320, TFT_DARKGREY);
  for (int i = 0; i < 27; ++i) {
    const auto& tick = ticks[i];
    if (!tick.valid) continue;
    const bool major = tick.name || tick.endpoint || (tick.hz && (tick.hz / tick.step) % 5 == 0);
    const bool adjacent = tick.band != model.band;
    // Label markers remain visible; decorative spacing has no tick marks.
    if (tick.hz || tick.name) g.drawFastVLine(tick.x, 140, major ? 12 : 6, adjacent ? TFT_CYAN : major ? TFT_LIGHTGREY : TFT_DARKGREY);
    if (!major) continue;
    char label[20];
    if (tick.name) snprintf(label, sizeof label, "%s", tick.name);
    else {
      const auto value = radio_format_frequency(tick.hz);
      snprintf(label, sizeof label, "%s%c", value.number, tick.hz >= 1000000 ? 'M' : 'k');
    }
    const int width = g.textWidth(label);
    // Adjacent band names face away from one another, each anchored to its tick.
    const bool right = tick.name && i + 1 < 27 && ticks[i + 1].name;
    const bool left = tick.name && i > 0 && ticks[i - 1].name;
    const int labelLeft = tick.x - (right ? width : left ? 0 : width / 2);
    const int labelRight = labelLeft + width;
    if (labelRight >= 160 - currentWidth / 2 - 6 && labelLeft <= 160 + currentWidth / 2 + 6) continue;
    bool collision = false;
    if (!tick.endpoint && !tick.name) {
      for (const auto& other : ticks) {
        if (!other.valid || !other.endpoint) continue;
        const auto value = radio_format_frequency(other.hz);
        char endpoint[20]; snprintf(endpoint, sizeof endpoint, "%s%c", value.number, other.hz >= 1000000 ? 'M' : 'k');
        const int endpointWidth = g.textWidth(endpoint);
        if (labelRight >= other.x - endpointWidth / 2 - 2 && labelLeft <= other.x + endpointWidth / 2 + 2) collision = true;
      }
    }
    if (collision) continue;
    g.setTextDatum(right ? lgfx::textdatum_t::top_right : left ? lgfx::textdatum_t::top_left : lgfx::textdatum_t::top_center);
    g.setTextColor(adjacent ? TFT_CYAN : TFT_LIGHTGREY, TFT_BLACK); g.drawString(label, tick.x, 159);
  }
  g.setTextDatum(lgfx::textdatum_t::top_center);
  g.fillTriangle(155, 133, 165, 133, 160, 139, TFT_RED);
  g.drawFastVLine(160, 140, 14, TFT_RED);
  g.setTextColor(TFT_RED, TFT_BLACK); g.drawString(current, 160, 159);
  g.setTextDatum(lgfx::textdatum_t::top_left);
}
}

// The layout occupies the top 320 x 170 on every board. Buffering only that
// keeps the frame within the largest internal-RAM block on boards without PSRAM.
constexpr int frameWidth = 320, frameHeight = 170;
static_assert(frameWidth <= BOARD_SCREEN_WIDTH && frameHeight <= BOARD_SCREEN_HEIGHT, "layout exceeds the panel");

constexpr int extrasHeight = BOARD_SCREEN_HEIGHT - frameHeight;

// Audio spectrum in the extras area: 32 bars over 0-8 kHz, styled after the V5A's original firmware.
void drawSpectrum(lgfx::LGFXBase& g, int top, const uint8_t* bars) {
  constexpr int left = 12, right = 310, floor = 44;
  g.drawFastHLine(8, top + floor + 1, 304, TFT_DARKGREY);
  for (int i = 0; i < SPECTRUM_BARS; ++i) {
    const int x = left + i * (right - left) / SPECTRUM_BARS, next = left + (i + 1) * (right - left) / SPECTRUM_BARS;
    const int h = bars[i] * 40 / 255;
    if (h) g.fillRect(x, top + floor - h, next - x - 1, h, TFT_CYAN);
  }
  g.setTextColor(TFT_DARKGREY, TFT_BLACK);
  g.setTextDatum(lgfx::textdatum_t::top_left); g.drawString("0", left, top + floor + 4);
  g.setTextDatum(lgfx::textdatum_t::top_center); g.drawString("4k", (left + right) / 2, top + floor + 4);
  g.setTextDatum(lgfx::textdatum_t::top_right); g.drawString("8kHz", right, top + floor + 4);
}

// Board-only information below the shared layout. Redrawn only when it changes.
void drawExtras(const ui_view_t* view) {
  char environment[32], line[64];
  if (!view->environment_known) environment[0] = 0;
  else if (!view->environment.valid) snprintf(environment, sizeof environment, "--.-C --.-%%RH");
  else {
    const int32_t tenths = (view->environment.milli_celsius + (view->environment.milli_celsius < 0 ? -50 : 50)) / 100;
    const uint32_t humidity = (view->environment.milli_percent + 50) / 100;
    snprintf(environment, sizeof environment, "%s%ld.%ldC %lu.%lu%%RH", tenths < 0 ? "-" : "", labs(tenths) / 10,
             labs(tenths) % 10, (unsigned long)(humidity / 10), (unsigned long)(humidity % 10));
  }
  snprintf(line, sizeof line, "%s|%s", view->date_time ? view->date_time : "", environment);
  const bool spectrum_changed = view->spectrum &&
      (!spectrum_drawn || memcmp(view->spectrum, spectrum_shown, sizeof spectrum_shown));
  if (!strcmp(line, extras_shown) && !spectrum_changed) return;
  snprintf(extras_shown, sizeof extras_shown, "%s", line);
  if (view->spectrum) { memcpy(spectrum_shown, view->spectrum, sizeof spectrum_shown); spectrum_drawn = true; }
  lgfx::LGFXBase& g = extras_buffered ? static_cast<lgfx::LGFXBase&>(extras) : static_cast<lgfx::LGFXBase&>(board_display_device());
  const int top = extras_buffered ? 0 : frameHeight, baseline = top + extrasHeight - 12;
  g.fillRect(0, top, frameWidth, extrasHeight, TFT_BLACK);
  g.setFont(&fonts::Font0); g.setTextSize(1);
  if (view->spectrum) drawSpectrum(g, top, view->spectrum);
  g.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g.setTextDatum(lgfx::textdatum_t::top_left);
  if (view->date_time) g.drawString(view->date_time, 8, baseline);
  g.setTextDatum(lgfx::textdatum_t::top_right);
  g.drawString(environment, frameWidth - 8, baseline);
  g.setTextDatum(lgfx::textdatum_t::top_left);
  if (extras_buffered) extras.pushSprite(0, frameHeight);
}

void ui_draw_extras(const ui_view_t* view) {
  if (extrasHeight >= 16) drawExtras(view);
}

bool ui_begin(void) {
  frame.setColorDepth(16);
  buffered = frame.createSprite(frameWidth, frameHeight) != nullptr;
  if (extrasHeight >= 16) {
    extras.setColorDepth(16);
    extras_buffered = extras.createSprite(frameWidth, extrasHeight) != nullptr;
  }
  return buffered;
}
void ui_draw(const ui_view_t* view) {
  const radio_model_t& model = *view->model;
  const radio_band_t* band = radio_model_band(&model);
  const si473x_mode_t currentMode = radio_model_mode(&model);
  const uint32_t step = radio_model_step(&model);
  lgfx::LGFXBase& g = buffered ? static_cast<lgfx::LGFXBase&>(frame) : static_cast<lgfx::LGFXBase&>(board_display_device());
  g.fillScreen(TFT_BLACK); g.setFont(&fonts::Font0); g.setTextSize(1);
  if (!buffered) { extras_shown[0] = 0; spectrum_drawn = false; } // Direct drawing cleared the extras too.
  // All bands remain visible in the left half of the single-line status bar.
  for (size_t i = 0; i < RADIO_BAND_COUNT; ++i) {
    const int x = 3 + i * 28;
    const bool active = model.band == i;
    const uint16_t color = !radio_bands[i].available ? TFT_DARKGREY : active ? (view->field == UI_FIELD_BAND ? TFT_YELLOW : TFT_CYAN) : TFT_LIGHTGREY;
    g.drawRect(x, 2, 26, 17, color);
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setTextColor(active && view->band_locked ? TFT_ORANGE : color, TFT_BLACK);
    g.drawString(radio_bands[i].name, x + 13, 10);
  }
  g.setTextDatum(lgfx::textdatum_t::top_left);
  if (view->clock) {
    g.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    g.setCursor(152, 7); g.print(view->clock);
  }
  uint16_t color = view->field == UI_FIELD_BRIGHTNESS ? TFT_YELLOW : TFT_LIGHTGREY;
  drawSun(g, 190, color); g.setTextColor(color, TFT_BLACK);
  g.setCursor(206, 7); g.printf("%d%%", (view->brightness * 100 + 127) / 255);
  const uint16_t speakerColor = view->muted ? TFT_RED : !view->audio_status_known ? TFT_DARKGREY :
                                view->signal.stereo ? TFT_CYAN : TFT_LIGHTGREY;
  drawSpeaker(g, 232, speakerColor, view->muted, view->volume);
  color = view->field == UI_FIELD_VOLUME ? TFT_YELLOW : view->muted ? TFT_RED : TFT_LIGHTGREY;
  g.setTextColor(color, TFT_BLACK);
  g.setCursor(248, 7); g.printf("%d%%", (view->volume * 100 + 31) / 63);
  const auto& supply = view->supply;
  color = !supply.plausible ? TFT_RED : supply.external_power_likely ? TFT_CYAN : supply.estimated_percent < 20 ? TFT_RED : TFT_LIGHTGREY;
  g.drawRect(274, 6, 13, 9, color); g.drawFastVLine(287, 8, 5, color);
  if (supply.plausible) g.fillRect(276, 8, supply.external_power_likely ? 9 : supply.estimated_percent * 9 / 100, 5, color);
  // The text beside the icon shows the percentage or, when chosen, the voltage ("4.20V" fits to the edge).
  g.setTextColor(view->field == UI_FIELD_BATTERY ? TFT_YELLOW : color, TFT_BLACK); g.setCursor(290, 7);
  if (!supply.plausible) g.print(view->battery_volts ? "-.--V" : "--%");
  else if (view->battery_volts) g.printf("%.2fV", supply.volts);
  else if (supply.external_power_likely) g.print("USB");
  else g.printf("%u%%", supply.estimated_percent);

  const auto frequency = radio_format_frequency(radio_model_frequency(&model));
  const bool failed = view->failed, pending = view->pending;
  g.setTextColor(failed ? TFT_RED : view->seek_mode ? TFT_MAGENTA : view->field == UI_FIELD_FREQUENCY ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
  const bool rdsVisible = !failed && !pending;
  const bool showPs = rdsVisible && view->ps[0];
  const bool showRt = rdsVisible && view->message[0];
  const bool splitRds = showPs && showRt;
  constexpr int textTop = 22, textHeight = 46, regionWidth = 304;
  const uint32_t textNow = millis();
  const auto drawText = [&](const char* text, int top, int height, int slot) {
    auto& state = scroll[slot];
    const int width = g.textWidth(text);
    if (strcmp(state.text, text) || state.width != width) {
      snprintf(state.text, sizeof state.text, "%s", text);
      state.width = width; state.started = textNow;
    }
    int offset = 0;
    if (width > regionWidth) {
      const uint32_t travel = (width - regionWidth) * 35u;
      const uint32_t phase = (textNow - state.started) % (travel + 2400);
      offset = phase < 1200 ? 0 : phase < travel + 1200 ? (phase - 1200) / 35 : width - regionWidth;
    }
    g.setClipRect(8, top, regionWidth, height);
    g.setTextDatum(lgfx::textdatum_t::middle_left);
    g.drawString(text, 8 - offset, top + height / 2);
    g.clearClipRect();
  };
  if (showPs || showRt) {
    g.setFont(&fonts::efontCN_24_b); g.setTextSize(1);
    const int rowHeight = splitRds ? textHeight / 2 : textHeight;
    g.setTextSize(float(rowHeight - (splitRds ? 2 : 4)) / g.fontHeight());
    drawText(showPs ? view->ps : view->message, textTop, rowHeight, 0);
    if (splitRds) drawText(view->message, textTop + rowHeight, rowHeight, 1);
  } else {
    g.setFont(&fonts::efontCN_24_b); g.setTextSize(1);
    lgfx::FontMetrics metrics{}; fonts::efontCN_24_b.getDefaultMetric(&metrics);
    const float scale = float(textHeight - 4) / metrics.height;
    g.setTextSize(scale);
    const int width = g.textWidth(frequency.number);
    const int baseline = textTop + (textHeight - g.fontHeight()) / 2 + int(metrics.baseline * scale);
    g.setClipRect(8, textTop, regionWidth, textHeight);
    g.setTextDatum(lgfx::textdatum_t::baseline_left);
    g.drawString(frequency.number, 8, baseline);
    constexpr float unitScale = 0.5f;
    // efont's digits and MHz/kHz glyphs sit 3 native pixels above its Unicode
    // baseline. Compensate the different scales so their ink baselines match.
    const auto latinBaselineShift = [&](float size) {
      return int((metrics.baseline - 3) * size) - int(metrics.baseline * size);
    };
    const int unitBaseline = baseline + latinBaselineShift(scale) - latinBaselineShift(unitScale);
    g.setTextSize(unitScale);
    g.drawString(frequency.unit, 8 + width + 4, unitBaseline);
    g.clearClipRect(); scroll[0] = {};
  }
  if (!splitRds) scroll[1] = {};
  g.setFont(&fonts::Font0); g.setTextSize(1);
  if (view->seek_arrow > 0) g.fillTriangle(303, 39, 303, 53, 312, 46, TFT_MAGENTA);
  else if (view->seek_arrow < 0) g.fillTriangle(294, 39, 294, 53, 285, 46, TFT_MAGENTA);
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  for (int i = 0; i < 4; ++i) {
    const auto mode = static_cast<si473x_mode_t>(i); const int x = 3 + i * 28;
    const bool active = currentMode == mode;
    const uint16_t ink = !radio_mode_allowed(band, mode) ? TFT_DARKGREY : active ? (view->field == UI_FIELD_MODE ? TFT_YELLOW : TFT_CYAN) : TFT_LIGHTGREY;
    g.drawRect(x, 70, 26, 17, ink);
    g.setTextColor(ink, TFT_BLACK); g.drawString(si473x_mode_name(mode), x + 13, 78);
  }
  const auto& signal = view->signal;
  const auto& reception = view->reception;
  const bool stable = !failed && !pending && signal.quality_known;
  const auto option = [&](ui_field_t focus, int x, int width, const char* label) {
    const bool smActive = focus == UI_FIELD_SOFT_MUTE && stable && reception.soft_mute_db && signal.soft_muted;
    const uint16_t ink = view->field == focus ? TFT_YELLOW : TFT_CYAN;
    g.drawRect(x, 70, width, 17, ink); g.setTextColor(smActive ? TFT_ORANGE : ink, TFT_BLACK);
    g.drawString(label, x + width / 2, 78);
  };
  char label[24]; int count; const auto* options = si473x_bandwidths(currentMode, &count);
  const uint32_t bw = options[reception.bandwidth].hz;
  if (!bw) snprintf(label, sizeof label, "BW AUTO");
  else if (bw >= 10000) snprintf(label, sizeof label, "BW %luk", static_cast<unsigned long>(bw / 1000));
  else snprintf(label, sizeof label, "BW %lu.%luk", static_cast<unsigned long>(bw / 1000), static_cast<unsigned long>(bw % 1000 / 100));
  option(UI_FIELD_BANDWIDTH, 117, 63, label);
  if (reception.gain < 0) snprintf(label, sizeof label, "AGC AUTO"); else snprintf(label, sizeof label, "ATT %d", reception.gain);
  option(UI_FIELD_GAIN, 184, 70, label);
  if (!reception.soft_mute_db) snprintf(label, sizeof label, "SM OFF"); else snprintf(label, sizeof label, "SM %udB", reception.soft_mute_db);
  option(UI_FIELD_SOFT_MUTE, 258, 59, label);
  g.setTextDatum(lgfx::textdatum_t::top_left);
  const int bars = stable ? constrain((signal.rssi + 5) / 6, 0, 10) : 0;
  for (int i = 0; i < 10; ++i)
    g.fillRect(8 + i * 3, 95, 2, 8, i < bars ? TFT_GREEN : TFT_DARKGREY);
  g.setTextColor(TFT_LIGHTGREY, TFT_BLACK); g.setCursor(42, 95);
  if (stable) g.printf("%udBuV", signal.rssi); else g.print("--dBuV");
  g.setCursor(90, 95); if (stable) g.printf("SN%udB", signal.snr); else g.print("SN--dB");
  g.setCursor(138, 95);
  g.setTextColor(failed ? TFT_RED : stable && signal.valid ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  if (failed) g.print("ERROR");
  else if (pending) g.print(view->seeking ? "SEEK" : "TUNING");
  else if (stable) g.print(signal.valid ? "VALID" : "INVALID"); else g.print("--");

  const uint16_t stepInk = view->field == UI_FIELD_STEP ? TFT_YELLOW : TFT_CYAN;
  g.drawRect(278, 91, 39, 17, stepInk); g.setTextColor(stepInk, TFT_BLACK);
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  if (step >= 1000) snprintf(label, sizeof label, "%lukHz", static_cast<unsigned long>(step / 1000));
  else snprintf(label, sizeof label, "%luHz", static_cast<unsigned long>(step));
  g.drawString(label, 297, 99); g.setTextDatum(lgfx::textdatum_t::top_left);
  g.setTextColor(stable && signal.afc_rail ? TFT_RED : TFT_LIGHTGREY, TFT_BLACK); g.setCursor(186, 95);
  g.print(!stable ? "AFC --" : signal.afc_rail ? "AFC!" : "AFC OK");
  g.setTextColor(TFT_LIGHTGREY, TFT_BLACK); g.setCursor(228, 95);
  if (stable) g.printf("RF%u", signal.gain_index); else g.print("RF--");
  if (band->fm) {
    g.setCursor(258, 95); if (stable) g.printf("P%u", signal.pilot); else g.print("P--");
  }
  if (failed) { g.setTextColor(TFT_RED, TFT_BLACK); g.setCursor(8, 115); g.print(view->error); }
  else if (view->notice) { g.setTextColor(TFT_YELLOW, TFT_BLACK); g.setCursor(8, 115); g.print(view->notice); }
  drawRuler(g, model, view->band_locked);
  if (buffered) frame.pushSprite(0, 0);
  if (extrasHeight >= 16) drawExtras(view);
}
void ui_capture(void) {
  if (!buffered) { serial_println("SCREENSHOT_ERROR framebuffer unavailable"); return; }
  // The shared frame, followed by the extras area when it has its own buffer.
  constexpr int width = frameWidth;
  const int height = frameHeight + (extras_buffered ? extrasHeight : 0);
  serial_printf("\nATS_SCREENSHOT RGB888 %d %d %d\n", width, height, width * height * 3);
  lgfx::bgr888_t pixels[width]; uint8_t row[width * 3];
  for (int y = 0; y < height; ++y) {
    if (y < frameHeight) frame.readRectRGB(0, y, width, 1, pixels);
    else extras.readRectRGB(0, y - frameHeight, width, 1, pixels);
    for (int x = 0; x < width; ++x) {
      row[x * 3] = pixels[x].r; row[x * 3 + 1] = pixels[x].g; row[x * 3 + 2] = pixels[x].b;
    }
    size_t sent = 0; const uint32_t started = millis();
    while (sent < sizeof row) {
      sent += serial_write(row + sent, sizeof row - sent);
      if (millis() - started > 2000) return;
      if (sent < sizeof row) delay(1);
    }
  }
  serial_print("\nATS_SCREENSHOT_END\n");
}

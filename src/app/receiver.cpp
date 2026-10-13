#include "receiver.h"
#include "drivers/si473x/si473x.h"
#include "radio/model.h"
#include "hal/audio.h"
#include "hal/battery.h"
#include "hal/board.h"
#include "hal/button.h"
#include "hal/display.h"
#include "hal/encoder.h"
#include "hal/radio.h"
namespace {
hal::Display display;
lgfx::LGFX_Sprite frame(&display);
hal::Battery battery;
hal::BatteryReading supply{};
hal::Encoder encoder;
hal::Button button(hal::board::encoderButton);
hal::Audio audio;
hal::RadioHardware hardware;
drivers::Si473x chip(hardware.bus());
radio::Model model;
enum class Field { Frequency, Band, Mode, Step, Volume, Brightness };
Field field = Field::Frequency;
int volume = 18, appliedVolume = -1, brightness = 128;
bool muted = false, dirty = true, lcd = false, buffered = false, failed = false;
bool reconfigure = true, held = false, previousPressed = false, audioReady = false;
uint32_t pressedAt = 0, editedAt = 0, lastSignal = 0, lastBattery = 0, lastDraw = 0, lastTune = 0;
void report() {
  const auto& s = chip.signal();
  Serial.printf("RADIO band=%s mode=%s requested=%lu confirmed=%lu step=%lu volume=%d mute=%d tuning=%d RSSI=%u SNR=%u error=%s\n",
    model.band().name, drivers::modeName(model.mode()), static_cast<unsigned long>(model.frequencyHz()),
    static_cast<unsigned long>(chip.frequencyHz()), static_cast<unsigned long>(model.stepHz()), volume, muted,
    chip.tuning(), s.rssi, s.snr, chip.errorText());
}
void drawSun(lgfx::LGFXBase& g, int x, uint16_t color) {
  g.drawCircle(x + 6, 10, 3, color);
  g.drawLine(x + 6, 3, x + 6, 5, color); g.drawLine(x + 6, 15, x + 6, 17, color);
  g.drawLine(x - 1, 10, x + 1, 10, color); g.drawLine(x + 11, 10, x + 13, 10, color);
  g.drawLine(x + 1, 5, x + 2, 6, color); g.drawLine(x + 10, 14, x + 11, 15, color);
  g.drawLine(x + 1, 15, x + 2, 14, color); g.drawLine(x + 10, 6, x + 11, 5, color);
}
void drawSpeaker(lgfx::LGFXBase& g, int x, uint16_t color) {
  g.drawRect(x, 8, 4, 5, color);
  g.drawLine(x + 4, 8, x + 8, 5, color); g.drawLine(x + 8, 5, x + 8, 15, color);
  g.drawLine(x + 8, 15, x + 4, 12, color);
  if (muted) { g.drawLine(x + 10, 7, x + 14, 13, color); g.drawLine(x + 14, 7, x + 10, 13, color); }
  else { g.drawLine(x + 11, 7, x + 13, 9, color); g.drawLine(x + 13, 9, x + 13, 11, color); g.drawLine(x + 13, 11, x + 11, 13, color); }
}
void drawRuler(lgfx::LGFXBase& g) {
  g.drawFastHLine(0, 130, 320, TFT_DARKGREY);
  g.setTextSize(1); g.setTextDatum(lgfx::textdatum_t::top_center);
  for (int offset = -13; offset <= 13; ++offset) {
    radio::Model preview = model;
    preview.turnFrequency(offset); // Same cross-band semantics as the encoder.
    const uint32_t hz = preview.frequencyHz();
    const int x = 160 + offset * 12;
    const bool major = (hz / preview.stepHz()) % 5 == 0;
    const bool boundary = preview.bandIndex() != model.bandIndex();
    g.drawFastVLine(x, 130, major ? 17 : 8, boundary ? TFT_CYAN : major ? TFT_LIGHTGREY : TFT_DARKGREY);
    if (major) {
      char label[20];
      if (preview.band().fm) snprintf(label, sizeof label, "%lu.%02luM", static_cast<unsigned long>(hz / 1000000), static_cast<unsigned long>(hz % 1000000 / 10000));
      else if (preview.stepHz() < 1000) snprintf(label, sizeof label, "%lu.%02luk", static_cast<unsigned long>(hz / 1000), static_cast<unsigned long>(hz % 1000 / 10));
      else snprintf(label, sizeof label, "%luk", static_cast<unsigned long>(hz / 1000));
      g.setTextColor(boundary ? TFT_CYAN : TFT_LIGHTGREY, TFT_BLACK); g.drawString(label, x, 151);
    }
  }
  g.fillTriangle(155, 123, 165, 123, 160, 129, TFT_RED);
  g.drawFastVLine(160, 130, 18, TFT_RED);
  g.setTextDatum(lgfx::textdatum_t::top_left);
}
void draw() {
  lgfx::LGFXBase& g = buffered ? static_cast<lgfx::LGFXBase&>(frame) : static_cast<lgfx::LGFXBase&>(display);
  g.fillScreen(TFT_BLACK); g.setTextSize(1);
  // All bands remain visible in the left half of the single-line status bar.
  for (size_t i = 0; i < radio::bandCount; ++i) {
    const int x = 3 + i * 28;
    const bool active = model.bandIndex() == i;
    const uint16_t color = !radio::bands[i].available ? TFT_DARKGREY : active ? (field == Field::Band ? TFT_YELLOW : TFT_CYAN) : TFT_LIGHTGREY;
    g.drawRoundRect(x, 2, 26, 17, 3, color);
    g.setTextDatum(lgfx::textdatum_t::middle_center); g.setTextColor(color, TFT_BLACK);
    g.drawString(radio::bands[i].name, x + 13, 10);
  }
  g.setTextDatum(lgfx::textdatum_t::top_left);
  uint16_t color = field == Field::Brightness ? TFT_YELLOW : TFT_LIGHTGREY;
  drawSun(g, 176, color); g.setTextColor(color, TFT_BLACK);
  g.setCursor(192, 7); g.printf("%d%%", (brightness * 100 + 127) / 255);
  color = field == Field::Volume ? TFT_YELLOW : muted ? TFT_RED : TFT_LIGHTGREY;
  drawSpeaker(g, 224, color); g.setTextColor(color, TFT_BLACK);
  g.setCursor(241, 7); g.printf("%d%%", (volume * 100 + 31) / 63);
  color = !supply.plausible ? TFT_RED : supply.externalPowerLikely ? TFT_CYAN : supply.estimatedPercent < 20 ? TFT_RED : TFT_LIGHTGREY;
  g.drawRect(272, 6, 13, 9, color); g.drawFastVLine(285, 8, 5, color);
  if (supply.plausible && !supply.externalPowerLikely) g.fillRect(274, 8, supply.estimatedPercent * 9 / 100, 5, color);
  g.setTextColor(color, TFT_BLACK); g.setCursor(289, 7);
  if (!supply.plausible || supply.externalPowerLikely) g.print("--%"); else g.printf("%u%%", supply.estimatedPercent);

  const uint32_t hz = model.frequencyHz(); char frequency[20];
  if (model.band().fm) snprintf(frequency, sizeof frequency, "%lu.%02lu", static_cast<unsigned long>(hz / 1000000), static_cast<unsigned long>(hz % 1000000 / 10000));
  else if (drivers::isSsb(model.mode())) snprintf(frequency, sizeof frequency, "%lu.%03lu", static_cast<unsigned long>(hz / 1000), static_cast<unsigned long>(hz % 1000));
  else snprintf(frequency, sizeof frequency, "%lu", static_cast<unsigned long>(hz / 1000));
  const bool pending = reconfigure || chip.tuning() || chip.frequencyHz() != hz;
  g.setTextColor(failed ? TFT_RED : field == Field::Frequency ? TFT_CYAN : TFT_WHITE, TFT_BLACK);
  g.setTextSize(4); g.setTextDatum(lgfx::textdatum_t::middle_center); g.drawString(frequency, 151, 49);
  g.setTextSize(1); g.setTextColor(pending ? TFT_DARKGREY : TFT_LIGHTGREY, TFT_BLACK); g.drawString(model.band().fm ? "MHz" : "kHz", 286, 57);
  for (int i = 0; i < 4; ++i) {
    const auto mode = static_cast<drivers::Mode>(i); const int x = 8 + i * 78;
    const bool active = model.mode() == mode;
    const uint16_t ink = !radio::allowed(model.band(), mode) ? TFT_DARKGREY : active ? (field == Field::Mode ? TFT_YELLOW : TFT_CYAN) : TFT_LIGHTGREY;
    g.drawRoundRect(x, 79, 70, 22, 3, ink);
    g.setTextColor(ink, TFT_BLACK); g.drawString(drivers::modeName(mode), x + 35, 90);
  }
  g.setTextDatum(lgfx::textdatum_t::top_left);
  // Ten bars are a visual RSSI indicator, not a calibrated S-meter.
  const int bars = failed || pending ? 0 : constrain((chip.signal().rssi + 5) / 6, 0, 10);
  for (int i = 0; i < 10; ++i) {
    const int height = 3 + i;
    g.fillRect(8 + i * 5, 120 - height, 3, height, i < bars ? TFT_GREEN : TFT_DARKGREY);
  }
  g.setTextColor(TFT_LIGHTGREY, TFT_BLACK); g.setCursor(66, 111);
  if (failed) g.print(chip.errorText()); else g.printf("S/N %u dB", chip.signal().snr);
  if (!failed) {
    g.setTextColor(field == Field::Step ? TFT_YELLOW : TFT_LIGHTGREY, TFT_BLACK); g.setCursor(222, 111);
    if (model.stepHz() >= 1000) g.printf("%lu kHz", static_cast<unsigned long>(model.stepHz() / 1000));
    else g.printf("%lu Hz", static_cast<unsigned long>(model.stepHz()));
  }
  drawRuler(g);
  if (buffered) frame.pushSprite(0, 0);
}
void fail() {
  failed = true; audioReady = false; audio.setMuted(true); audio.setSpeakerEnabled(false);
  dirty = true; report();
}
void changedConfiguration() { reconfigure = true; editedAt = millis(); dirty = true; }
void retry() {
  audio.setMuted(true); audio.setSpeakerEnabled(false);
  hardware.reset(); failed = !chip.begin(); appliedVolume = -1;
  audioReady = false; changedConfiguration();
}
void tuneBy(int delta) {
  const size_t before = model.bandIndex();
  model.turnFrequency(delta);
  if (before != model.bandIndex()) changedConfiguration();
  dirty = true;
}
void turn(int delta) {
  switch (field) {
    case Field::Frequency: tuneBy(delta); break;
    case Field::Band: model.turnBand(delta); changedConfiguration(); break;
    case Field::Mode: model.turnMode(delta); changedConfiguration(); break;
    case Field::Step: model.turnStep(delta); break;
    case Field::Volume: volume = constrain(volume + delta, 0, 63); break;
    case Field::Brightness: brightness = constrain(brightness + delta * 8, 8, 255); if (lcd) display.setBrightness(brightness); break;
  }
  dirty = true;
}
// Single-character diagnostics for repeatable real-device checks.
void serialInput() {
  for (int count = 0; count < 16 && Serial.available(); ++count) {
    const char c = Serial.read();
    if (c >= '0' && c <= '3') { model.setBand(c - '0'); changedConfiguration(); }
    else if (c == 'a' || c == 'l' || c == 'u' || c == 'f') {
      const auto mode = c == 'a' ? drivers::Mode::AM : c == 'l' ? drivers::Mode::LSB : c == 'u' ? drivers::Mode::USB : drivers::Mode::FM;
      if (model.setMode(mode)) changedConfiguration();
      else Serial.println("Mode unavailable on this band");
    } else if (c == '+' || c == '-') { tuneBy(c == '+' ? 1 : -1); }
    else if (c == '[' || c == ']') {
      model.setFrequency(c == '[' ? model.band().minHz : model.band().maxHz); dirty = true;
    }
    else if (c == 'm') { muted = !muted; if (audioReady) audio.setMuted(muted); dirty = true; }
    else if (c == '?') report();
    else if (c == 'r') retry();
  }
}
}
namespace app {
void beginReceiver() {
  Serial.begin(115200); audio.begin(); battery.begin(); encoder.begin(); button.begin();
  lcd = display.begin();
  if (lcd) { frame.setColorDepth(16); buffered = frame.createSprite(320, 170) != nullptr; }
  supply = battery.read();
  hardware.begin(); hardware.setPowered(true); hardware.reset();
  // Bound each bus transaction independently of the command-level CTS deadline.
  hardware.bus().setTimeOut(20);
  failed = !chip.begin();
  Serial.printf("Receiver boot: LCD=%d I2C=0x%02x found=%d\n", lcd, chip.address(), !failed);
  editedAt = millis() - 200;
  if (failed) fail();
}
void updateReceiver() {
  const uint32_t now = millis();
  button.update(now);
  if (button.pressed() && !previousPressed) { pressedAt = now; held = false; }
  if (button.pressed() && !held && now - pressedAt >= 650) {
    held = true; muted = !muted; if (audioReady) audio.setMuted(muted); dirty = true;
  }
  if (!button.pressed() && previousPressed && !held) {
    if (failed) retry();
    else field = static_cast<Field>((static_cast<int>(field) + 1) % 6);
    dirty = true;
  }
  previousPressed = button.pressed();
  const int delta = encoder.update(); if (delta) turn(delta);
  serialInput();
  if (!failed && reconfigure && now - editedAt >= 180) {
    audio.setMuted(true); audioReady = false;
    if (!chip.configure(model.mode(), volume)) fail();
    else {
      reconfigure = false; appliedVolume = volume;
      Serial.printf("Configured mode=%s part=%02x fw=%02x.%02x component=%c.%c patch=0x%04x\n", drivers::modeName(model.mode()), chip.revision().part,
        chip.revision().major, chip.revision().minor, chip.revision().componentMajor, chip.revision().componentMinor, chip.revision().patch);
    }
    dirty = true;
  }
  if (!failed && !reconfigure) {
    const bool wasTuning = chip.tuning();
    if (!chip.poll()) fail();
    if (wasTuning && !chip.tuning()) { dirty = true; report(); }
    if (!failed && !chip.tuning() && chip.frequencyHz() != model.frequencyHz() && now - lastTune >= 30) {
      lastTune = now;
      if (!chip.tune(model.frequencyHz())) fail();
      dirty = true;
    }
    if (!failed && !chip.tuning() && chip.frequencyHz() == model.frequencyHz() && !audioReady) {
      audioReady = true; audio.setSpeakerEnabled(true); audio.setMuted(muted); dirty = true;
    }
    if (!failed && appliedVolume != volume) {
      if (!chip.setVolume(volume)) fail(); else appliedVolume = volume;
    }
    if (!failed && !chip.tuning() && now - lastSignal >= 500) {
      lastSignal = now; if (!chip.readSignal()) fail(); dirty = true;
    }
  }
  if (now - lastBattery >= 1000) { lastBattery = now; supply = battery.read(); dirty = true; }
  if (dirty && now - lastDraw >= 50) { dirty = false; lastDraw = now; if (lcd) draw(); }
  delay(1);
}
}

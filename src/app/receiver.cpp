#include "receiver.h"
#include "drivers/si473x/si473x.h"
#include "radio/model.h"
#include "radio/frequency.h"
#include "radio/saved_settings.h"
#include "hal/settings_storage.h"
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
enum class Field { Band, Frequency, Mode, Bandwidth, Gain, SoftMute, Step, Volume, Brightness, Count };
Field field = Field::Band;
int volume = 18, appliedVolume = -1;
int brightness = 128;
drivers::ReceptionSettings reception[] = {{0, -1, 16}, {4, -1, 0}, {3, -1, 0}, {3, -1, 0}};
bool receptionDirty = true;
hal::SettingsStorage storage;
radio::SettingsBytes observedSettings{}, storedSettings{};
bool storageReady = false, settingsStored = false, savePending = false;
uint32_t lastSettingsChange = 0, lastSaveAttempt = 0, settingsWrites = 0;
auto settings() -> drivers::ReceptionSettings& { return reception[static_cast<unsigned>(model.mode())]; }
bool muted = false, dirty = true, lcd = false, buffered = false, failed = false;
radio::SavedSettings savedSettings() {
  radio::SavedSettings saved; saved.tuning = model.state();
  saved.volume = volume; saved.brightness = brightness; saved.muted = muted;
  for (int i = 0; i < 4; ++i) saved.reception[i] = reception[i];
  return saved;
}
bool reconfigure = true, held = false, previousPressed = false, audioReady = false, seekMode = false;
int pendingSeek = 0, seekDirection = 0;
uint32_t pressedAt = 0, editedAt = 0, lastSignal = 0, lastBattery = 0, lastDraw = 0, lastTune = 0, lastRds = 0;
void loadSettings() {
  storageReady = storage.begin();
  radio::SettingsBytes bytes{}; radio::SavedSettings saved;
  if (storageReady && storage.load(bytes.data(), bytes.size()) && radio::decodeSettings(bytes, saved)) {
    model.restore(saved.tuning); volume = saved.volume; brightness = saved.brightness; muted = saved.muted;
    for (int i = 0; i < 4; ++i) reception[i] = saved.reception[i];
    settingsStored = true;
  }
  radio::encodeSettings(savedSettings(), observedSettings); storedSettings = observedSettings;
  Serial.printf("Settings: %s\n", !storageReady ? "NVS unavailable" : settingsStored ? "restored" : "defaults");
}
void rememberSettings(uint32_t now) {
  radio::SettingsBytes bytes{};
  if (!storageReady || !radio::encodeSettings(savedSettings(), bytes)) return;
  if (bytes != observedSettings) {
    observedSettings = bytes; lastSettingsChange = now;
    savePending = !settingsStored || observedSettings != storedSettings;
  }
  if (!savePending || now - lastSettingsChange < 2000 || chip.tuning() || chip.seeking() ||
      pendingSeek || (reconfigure && !failed) || (lastSaveAttempt && now - lastSaveAttempt < 10000)) return;
  lastSaveAttempt = now;
  if (storage.save(observedSettings.data(), observedSettings.size())) {
    storedSettings = observedSettings; settingsStored = true; savePending = false;
    lastSaveAttempt = 0; ++settingsWrites; Serial.println("Settings: saved");
  } else Serial.println("Settings: save failed; retry in 10s");
}
void report() {
  const auto& s = chip.signal();
  Serial.printf("RADIO band=%s mode=%s requested=%lu confirmed=%lu step=%lu volume=%d brightness=%d mute=%d stored=%d save=%d writes=%lu tuning=%d RSSI=%u SNR=%u valid=%d blend=%u pilot=%d agc=%d gain=%u sm=%d afc=%d bw=%lu focus=%u seek=%d searching=%d result=%u bwsel=%u gainset=%d smset=%u ps=[%s] error=%s\n",
    model.band().name, drivers::modeName(model.mode()), static_cast<unsigned long>(model.frequencyHz()),
    static_cast<unsigned long>(chip.frequencyHz()), static_cast<unsigned long>(model.stepHz()), volume, brightness, muted, settingsStored, savePending, static_cast<unsigned long>(settingsWrites),
    chip.tuning(), s.rssi, s.snr, s.valid, s.blend, s.pilot, s.agcEnabled, s.gainIndex, s.softMuted, s.afcRail, static_cast<unsigned long>(chip.bandwidthHz()), static_cast<unsigned>(field), seekMode, chip.seeking(), static_cast<unsigned>(chip.seekResult()), settings().bandwidth, settings().gain, settings().softMuteDb, chip.rdsPs(), chip.errorText());
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
  else if (chip.signal().qualityKnown && !reconfigure && !chip.tuning() && !chip.seeking()) {
    if (chip.signal().stereo) {
      g.drawFastVLine(x + 10, 9, 3, color);
      g.drawLine(x + 12, 6, x + 14, 8, color); g.drawFastVLine(x + 14, 8, 5, color);
      g.drawLine(x + 14, 12, x + 12, 14, color);
    } else {
      g.drawLine(x + 10, 8, x + 12, 9, color); g.drawFastVLine(x + 12, 9, 3, color);
      g.drawLine(x + 12, 11, x + 10, 12, color);
    }
  }
}
void drawRuler(lgfx::LGFXBase& g) {
  struct Tick { uint32_t hz, step; size_t band; int x; bool valid; };
  Tick ticks[27];
  constexpr int center = 13, spacing = 12;
  for (int i = 0; i < 27; ++i) {
    const int offset = i - center;
    radio::Model preview = model;
    preview.turnFrequency(offset);
    bool valid = true;
    if (offset) {
      radio::Model closer = model;
      closer.turnFrequency(offset > 0 ? offset - 1 : offset + 1);
      valid = closer.bandIndex() != preview.bandIndex() || closer.frequencyHz() != preview.frequencyHz();
    }
    ticks[i] = {preview.frequencyHz(), preview.stepHz(), preview.bandIndex(), 0, valid};
  }
  for (int i = 0; i < 27; ++i) ticks[i].x = 160 + (i - center) * spacing;

  const auto frequency = radio::formatFrequency(model.frequencyHz());
  char current[24]; snprintf(current, sizeof current, "%s%s", frequency.number, frequency.unit);
  const int currentWidth = g.textWidth(current);
  g.drawFastHLine(0, 140, 320, TFT_DARKGREY);
  g.setTextSize(1); g.setTextDatum(lgfx::textdatum_t::top_center);
  for (int i = 0; i < 27; ++i) {
    const auto& tick = ticks[i];
    if (!tick.valid || tick.x < 0 || tick.x >= 320) continue;
    const bool edge = (i > 0 && ticks[i - 1].valid && ticks[i - 1].band != tick.band) ||
                      (i < 26 && ticks[i + 1].valid && ticks[i + 1].band != tick.band);
    const bool major = edge || (tick.hz / tick.step) % 5 == 0;
    const bool adjacent = tick.band != model.bandIndex();
    g.drawFastVLine(tick.x, 140, major ? 12 : 6, adjacent ? TFT_CYAN : major ? TFT_LIGHTGREY : TFT_DARKGREY);
    if (major) {
      char label[20];
      if (edge) snprintf(label, sizeof label, "%s", radio::bands[tick.band].name);
      else {
        const auto tickFrequency = radio::formatFrequency(tick.hz);
        snprintf(label, sizeof label, "%s%c", tickFrequency.number, tick.hz >= 1000000 ? 'M' : 'k');
      }
      // Keep the SW endpoint label clear of the adjacent 26 MHz tick value.
      // Moving the number outward leaves tick positions and the continuous
      // scale unchanged.
      const int labelX = edge
          ? tick.x + (i > 0 && ticks[i - 1].band != tick.band ? 2 : -2)
          : (tick.band == 2 && tick.hz >= radio::bands[2].maxHz - 200000 ? tick.x - 10 : tick.x);
      const int labelWidth = g.textWidth(label);
      if (labelX + labelWidth / 2 < 160 - currentWidth / 2 - 6 || labelX - labelWidth / 2 > 160 + currentWidth / 2 + 6) {
        g.setTextColor(adjacent ? TFT_CYAN : TFT_LIGHTGREY, TFT_BLACK); g.drawString(label, labelX, 159);
      }
    }
  }
  g.fillTriangle(155, 133, 165, 133, 160, 139, TFT_RED);
  g.drawFastVLine(160, 140, 14, TFT_RED);
  g.setTextColor(TFT_RED, TFT_BLACK); g.drawString(current, 160, 159);
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
    g.drawRect(x, 2, 26, 17, color);
    g.setTextDatum(lgfx::textdatum_t::middle_center); g.setTextColor(color, TFT_BLACK);
    g.drawString(radio::bands[i].name, x + 13, 10);
  }
  g.setTextDatum(lgfx::textdatum_t::top_left);
  uint16_t color = field == Field::Brightness ? TFT_YELLOW : TFT_LIGHTGREY;
  drawSun(g, 196, color); g.setTextColor(color, TFT_BLACK);
  g.setCursor(212, 7); g.printf("%d%%", (brightness * 100 + 127) / 255);
  color = field == Field::Volume ? TFT_YELLOW : muted ? TFT_RED : TFT_LIGHTGREY;
  drawSpeaker(g, 238, color); g.setTextColor(color, TFT_BLACK);
  g.setCursor(254, 7); g.printf("%d%%", (volume * 100 + 31) / 63);
  color = !supply.plausible ? TFT_RED : supply.externalPowerLikely ? TFT_CYAN : supply.estimatedPercent < 20 ? TFT_RED : TFT_LIGHTGREY;
  g.drawRect(280, 6, 13, 9, color); g.drawFastVLine(293, 8, 5, color);
  if (supply.plausible && !supply.externalPowerLikely) g.fillRect(282, 8, supply.estimatedPercent * 9 / 100, 5, color);
  g.setTextColor(color, TFT_BLACK); g.setCursor(296, 7);
  if (!supply.plausible || supply.externalPowerLikely) g.print("--%"); else g.printf("%u%%", supply.estimatedPercent);

  const uint32_t hz = model.frequencyHz();
  const auto frequency = radio::formatFrequency(hz);
  const bool pending = reconfigure || chip.tuning() || chip.seeking() || chip.frequencyHz() != hz;
  g.setTextColor(failed ? TFT_RED : seekMode ? TFT_MAGENTA : field == Field::Frequency ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
  const bool showPs = !failed && !pending && !seekMode && chip.rdsPs()[0];
  g.setFont(&fonts::efontCN_24); g.setTextSize(1);
  const int rtWidth = g.textWidth(chip.rdsText());
  const uint32_t rtDuration = max(8000u, uint32_t(max(0, rtWidth - 304)) * 35u + 2400u);
  const bool showRt = !failed && !pending && !seekMode && chip.rdsText()[0] &&
                      (!showPs || millis() % (5000u + rtDuration) >= 5000u);
  const char* mainText = showRt ? chip.rdsText() : showPs ? chip.rdsPs() : frequency.number;
  if (showPs || showRt) g.setFont(&fonts::efontCN_24); else g.setFont(&fonts::Font0);
  g.setTextSize(showPs || showRt ? 1 : 4);
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  const int textWidth = g.textWidth(mainText), regionWidth = 304;
  static char previousText[65]{}; static uint32_t scrollStarted = 0;
  if (strcmp(previousText, mainText)) {
    snprintf(previousText, sizeof previousText, "%s", mainText); scrollStarted = millis();
  }
  int offset = 0;
  if (textWidth > regionWidth) {
    const uint32_t travel = (textWidth - regionWidth) * 35u;
    const uint32_t phase = (millis() - scrollStarted) % (travel + 2400);
    offset = phase < 1200 ? 0 : phase < travel + 1200 ? (phase - 1200) / 35 : textWidth - regionWidth;
  }
  g.setClipRect(8, 23, regionWidth, 38); g.drawString(mainText, 8 - offset, 43);
  g.setFont(&fonts::Font0); g.setTextSize(1);
  if (!showPs && !showRt) g.drawString(frequency.unit, 8 + textWidth + 3 - offset, 51);
  g.clearClipRect();
  if (seekMode && (chip.seeking() || pendingSeek)) {
    const int direction = pendingSeek ? pendingSeek : seekDirection;
    if (direction > 0) g.fillTriangle(303, 39, 303, 53, 312, 46, TFT_MAGENTA);
    else g.fillTriangle(294, 39, 294, 53, 285, 46, TFT_MAGENTA);
  }
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  for (int i = 0; i < 4; ++i) {
    const auto mode = static_cast<drivers::Mode>(i); const int x = 3 + i * 28;
    const bool active = model.mode() == mode;
    const uint16_t ink = !radio::allowed(model.band(), mode) ? TFT_DARKGREY : active ? (field == Field::Mode ? TFT_YELLOW : TFT_CYAN) : TFT_LIGHTGREY;
    g.drawRect(x, 70, 26, 17, ink);
    g.setTextColor(ink, TFT_BLACK); g.drawString(drivers::modeName(mode), x + 13, 78);
  }
  const auto& signal = chip.signal();
  const bool stable = !failed && !pending && signal.qualityKnown;
  const auto option = [&](Field focus, int x, int width, const char* label) {
    const bool smActive = focus == Field::SoftMute && stable && settings().softMuteDb && signal.softMuted;
    const uint16_t ink = field == focus ? TFT_YELLOW : TFT_CYAN;
    g.drawRect(x, 70, width, 17, ink); g.setTextColor(smActive ? TFT_ORANGE : ink, TFT_BLACK);
    g.drawString(label, x + width / 2, 78);
  };
  char label[24]; int count; const auto* options = drivers::bandwidths(model.mode(), count);
  const uint32_t bw = options[settings().bandwidth].hz;
  if (!bw) snprintf(label, sizeof label, "BW AUTO");
  else if (bw >= 10000) snprintf(label, sizeof label, "BW %luk", static_cast<unsigned long>(bw / 1000));
  else snprintf(label, sizeof label, "BW %lu.%luk", static_cast<unsigned long>(bw / 1000), static_cast<unsigned long>(bw % 1000 / 100));
  option(Field::Bandwidth, 117, 63, label);
  if (settings().gain < 0) snprintf(label, sizeof label, "AGC AUTO"); else snprintf(label, sizeof label, "ATT %d", settings().gain);
  option(Field::Gain, 184, 70, label);
  if (!settings().softMuteDb) snprintf(label, sizeof label, "SM OFF"); else snprintf(label, sizeof label, "SM %udB", settings().softMuteDb);
  option(Field::SoftMute, 258, 59, label);
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
  else if (pending) g.print(chip.seeking() ? "SEEK" : "TUNING");
  else if (stable) g.print(signal.valid ? "VALID" : "INVALID"); else g.print("--");

  const uint16_t stepInk = field == Field::Step ? TFT_YELLOW : TFT_CYAN;
  g.drawRect(278, 91, 39, 17, stepInk); g.setTextColor(stepInk, TFT_BLACK);
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  if (model.stepHz() >= 1000) snprintf(label, sizeof label, "%lukHz", static_cast<unsigned long>(model.stepHz() / 1000));
  else snprintf(label, sizeof label, "%luHz", static_cast<unsigned long>(model.stepHz()));
  g.drawString(label, 297, 99); g.setTextDatum(lgfx::textdatum_t::top_left);
  g.setTextColor(stable && signal.afcRail ? TFT_RED : TFT_LIGHTGREY, TFT_BLACK); g.setCursor(186, 95);
  g.print(!stable ? "AFC --" : signal.afcRail ? "AFC!" : "AFC OK");
  g.setTextColor(TFT_LIGHTGREY, TFT_BLACK); g.setCursor(228, 95);
  if (stable) g.printf("RF%u", signal.gainIndex); else g.print("RF--");
  if (model.band().fm) {
    g.setCursor(258, 95); if (stable) g.printf("P%u", signal.pilot); else g.print("P--");
  }
  if (failed) { g.setTextColor(TFT_RED, TFT_BLACK); g.setCursor(8, 115); g.print(chip.errorText()); }
  drawRuler(g);
  if (buffered) frame.pushSprite(0, 0);
}
void fail() {
  failed = true; seekMode = false; pendingSeek = seekDirection = 0; audioReady = false; audio.setMuted(true); audio.setSpeakerEnabled(false);
  dirty = true; report();
}
void changedConfiguration() {
  if (chip.seeking() && !chip.cancelSeek()) fail();
  seekMode = false; pendingSeek = seekDirection = 0;
  reconfigure = true; editedAt = millis(); dirty = true;
}
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
void stopSeek(bool manual) {
  pendingSeek = seekDirection = 0;
  if (chip.seeking()) {
    if (!chip.cancelSeek()) fail();
    else model.setFrequency(chip.frequencyHz());
  }
  if (manual) seekMode = false;
  dirty = true;
}
void requestSeek(int delta) {
  if (failed || drivers::isSsb(model.mode()) || !delta) return;
  const int direction = delta > 0 ? 1 : -1;
  if (chip.seeking() && direction == seekDirection) return; // No queued station skipping.
  if (chip.seeking()) stopSeek(false);
  pendingSeek = direction; dirty = true;
}
void adjustFrequency(int delta) {
  if (seekMode) requestSeek(delta); else tuneBy(delta);
}
void longPress() {
  if (field == Field::Frequency) {
    if (seekMode) stopSeek(true);
    else if (!failed && !drivers::isSsb(model.mode())) { seekMode = true; dirty = true; }
  } else if (field == Field::Volume) {
    muted = !muted; if (audioReady) audio.setMuted(muted); dirty = true;
  }
}
void shortPress() {
  if (failed) retry();
  else if (seekMode || chip.seeking() || pendingSeek) stopSeek(true);
  else { field = static_cast<Field>((static_cast<int>(field) + 1) % static_cast<int>(Field::Count)); dirty = true; }
}
void turn(int delta) {
  switch (field) {
    case Field::Frequency: adjustFrequency(delta); break;
    case Field::Band: model.turnBand(delta); changedConfiguration(); break;
    case Field::Mode: model.turnMode(delta); changedConfiguration(); break;
    case Field::Step: model.turnStep(delta); break;
    case Field::Volume: volume = constrain(volume + delta, 0, 63); break;
    case Field::Bandwidth: {
      int count; drivers::bandwidths(model.mode(), count);
      const int next = (int(settings().bandwidth) + delta % count + count) % count;
      settings().bandwidth = next; receptionDirty = true; break;
    }
    case Field::Gain: settings().gain = constrain(int(settings().gain) + delta, -1, drivers::maxGainIndex(model.mode())); receptionDirty = true; break;
    case Field::SoftMute: settings().softMuteDb = constrain(int(settings().softMuteDb) + delta, 0, 31); receptionDirty = true; break;
    case Field::Count: break;
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
    } else if (c == '+' || c == '-') { adjustFrequency(c == '+' ? 1 : -1); }
    else if (c == '[' || c == ']') {
      stopSeek(true); model.setFrequency(c == '[' ? model.band().minHz : model.band().maxHz); dirty = true;
    }
    else if (c == 'm') { muted = !muted; if (audioReady) audio.setMuted(muted); dirty = true; }
    else if (c == 'e') shortPress();
    else if (c == 'h') longPress();
    else if (c == '>') turn(1);
    else if (c == '<') turn(-1);
    else if (c == '?') report();
    else if (c == 'r') retry();
  }
}
}
namespace app {
void beginReceiver() {
  Serial.begin(115200); audio.begin(); battery.begin(); encoder.begin(); button.begin();
  loadSettings();
  lcd = display.begin();
  if (lcd) display.setBrightness(brightness);
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
  if (button.pressed() && !previousPressed) {
    pressedAt = now; held = false;
    if (chip.seeking() || pendingSeek) { stopSeek(true); held = true; }
  }
  if (button.pressed() && !held && now - pressedAt >= 650) {
    held = true; longPress();
  }
  if (!button.pressed() && previousPressed && !held) {
    shortPress();
  }
  previousPressed = button.pressed();
  const int delta = encoder.update(); if (delta) turn(delta);
  serialInput();
  if (!failed && reconfigure && now - editedAt >= 180) {
    audio.setMuted(true); audioReady = false;
    if (!chip.configure(model.mode(), volume)) fail();
    else {
      reconfigure = false; appliedVolume = volume; receptionDirty = true;
      Serial.printf("Configured mode=%s part=%02x fw=%02x.%02x component=%c.%c patch=0x%04x\n", drivers::modeName(model.mode()), chip.revision().part,
        chip.revision().major, chip.revision().minor, chip.revision().componentMajor, chip.revision().componentMinor, chip.revision().patch);
    }
    dirty = true;
  }
  if (!failed && !reconfigure) {
    const bool wasTuning = chip.tuning(), wasSeeking = chip.seeking();
    if (!chip.poll()) fail();
    if (!failed && wasSeeking) {
      model.setFrequency(chip.frequencyHz()); dirty = true;
      if (!chip.seeking()) { seekDirection = 0; report(); }
    }
    if (wasTuning && !chip.tuning()) { dirty = true; report(); }
    if (!failed && receptionDirty && !chip.tuning() && !chip.seeking()) {
      if (!chip.setReception(settings())) fail(); else receptionDirty = false;
      dirty = true;
    }
    if (!failed && pendingSeek && !chip.tuning() && !chip.seeking() && chip.frequencyHz() == model.frequencyHz()) {
      seekDirection = pendingSeek; pendingSeek = 0;
      audio.setMuted(true); audioReady = false;
      if (!chip.seek(seekDirection > 0, model.band().minHz, model.band().maxHz, model.stepHz())) fail();
      dirty = true;
    }
    if (!failed && !chip.tuning() && !chip.seeking() && chip.frequencyHz() != model.frequencyHz() && now - lastTune >= 30) {
      lastTune = now;
      if (!chip.tune(model.frequencyHz())) fail();
      dirty = true;
    }
    if (!failed && !chip.tuning() && !chip.seeking() && !pendingSeek && chip.frequencyHz() == model.frequencyHz() && !audioReady) {
      audioReady = true; audio.setSpeakerEnabled(true); audio.setMuted(muted); dirty = true;
    }
    if (!failed && appliedVolume != volume) {
      if (!chip.setVolume(volume)) fail(); else appliedVolume = volume;
    }
    if (!failed && !chip.tuning() && !chip.seeking() && now - lastRds >= 100) {
      lastRds = now; if (!chip.readRds()) fail(); dirty = true;
    }
    if (!failed && !chip.tuning() && !chip.seeking() && now - lastSignal >= 500) {
      lastSignal = now; if (!chip.readSignal()) fail(); dirty = true;
    }
  }
  if (now - lastBattery >= 1000) { lastBattery = now; supply = battery.read(); dirty = true; }
  if (chip.rdsText()[0] && now - lastDraw >= 50) dirty = true;
  rememberSettings(now);
  if (dirty && now - lastDraw >= 50) { dirty = false; lastDraw = now; if (lcd) draw(); }
  delay(1);
}
}

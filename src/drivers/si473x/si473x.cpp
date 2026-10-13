#include "si473x.h"
#include <Arduino.h>
#include "patches/ssb.h"
namespace drivers {
bool Si473x::fail(Error error) {
  rds_.clear(); error_ = error; configured_ = false; tuning_ = seeking_ = false; carrierKnown_ = false;
  return false;
}
const char* Si473x::errorText() const {
  constexpr const char* names[] = {"OK", "SI4732 not found", "I2C error", "SI4732 timeout",
                                  "Command rejected", "Invalid frequency", "SSB patch mismatch"};
  return names[static_cast<uint8_t>(error_)];
}
bool Si473x::begin() {
  rds_.clear(); error_ = Error::None; powered_ = configured_ = tuning_ = carrierKnown_ = false;
  confirmedHz_ = 0; revision_ = {}; signal_ = {}; seeking_ = false; seekResult_ = SeekResult::None;
  for (uint8_t address : {uint8_t(0x11), uint8_t(0x63)}) {
    bus_.beginTransmission(address);
    if (bus_.endTransmission() == 0) { address_ = address; return true; }
  }
  address_ = 0; return fail(Error::NoDevice);
}
bool Si473x::read(uint8_t* response, uint8_t size) {
  if (bus_.requestFrom(address_, size) != size) return fail(Error::Bus);
  for (uint8_t i = 0; i < size; ++i) response[i] = bus_.read();
  return true;
}
bool Si473x::waitReady(uint32_t timeout) {
  const uint32_t start = millis();
  do {
    uint8_t status;
    if (!read(&status, 1)) return false;
    if (status & 0x80) return (status & 0x40) ? fail(Error::Command) : true;
    delay(1);
  } while (millis() - start < timeout);
  return fail(Error::Timeout);
}
bool Si473x::command(const uint8_t* bytes, uint8_t size, uint8_t* response, uint8_t responseSize) {
  if (!address_) return fail(Error::NoDevice);
  if (!waitReady()) return false;
  bus_.beginTransmission(address_);
  bus_.write(bytes, size);
  if (bus_.endTransmission() != 0) return fail(Error::Bus);
  // Allow the device to deassert CTS for the new command. An immediate read
  // can still return the previous command's CTS/response (including GET_REV).
  delayMicroseconds(300);
  if (!waitReady()) return false;
  if (responseSize) {
    if (!read(response, responseSize)) return false;
    if (response[0] & 0x40) return fail(Error::Command);
  }
  return true;
}
bool Si473x::property(uint16_t id, uint16_t value) {
  const uint8_t bytes[] = {0x12, 0, uint8_t(id >> 8), uint8_t(id), uint8_t(value >> 8), uint8_t(value)};
  return command(bytes, sizeof bytes);
}
bool Si473x::powerUp(uint8_t function) {
  const uint8_t bytes[] = {0x01, function, 0x05}; // Crystal oscillator, analog L/R output
  if (!command(bytes, sizeof bytes)) return false;
  delay(10);
  powered_ = true;
  return true;
}
bool Si473x::getRevision() {
  const uint8_t cmd[] = {0x10}; uint8_t response[9];
  if (!command(cmd, 1, response, sizeof response)) return false;
  Serial.printf("GET_REV @%02x:", address_);
  for (uint8_t value : response) Serial.printf(" %02x", value);
  Serial.println();
  revision_ = {response[1], response[2], response[3], response[6], response[7], response[8],
               uint16_t((response[4] << 8) | response[5])};
  return true;
}
bool Si473x::loadSsb() {
  uint8_t response[8]; const uint8_t query[] = {0x01, 0x1f, 0x05};
  if (!command(query, sizeof query, response, sizeof response)) return false;
  Serial.print("LIBRARY:");
  for (uint8_t value : response) Serial.printf(" %02x", value);
  Serial.println();
  // amrx_6_0_1 is a component version, not a library ID. SI4732-A10
  // can report zero in library/part fields; verify PATCH ID after loading.
  Serial.printf("SSB library: part=%02x library=%02x chip=%02x\n", response[1], response[7], response[6]);
  // Query-library function returns to power-down automatically.
  if (!powerUp(0x31)) return false;
  delay(50);
  size_t index15 = 0;
  for (size_t offset = 0, line = 0; offset < sizeof ssb_patch_content; offset += 7, ++line) {
    uint8_t bytes[8] = {0x16};
    if (index15 < sizeof cmd_0x15 / sizeof cmd_0x15[0] && pgm_read_word(cmd_0x15 + index15) == line) {
      bytes[0] = 0x15; ++index15;
    }
    for (int i = 0; i < 7; ++i) bytes[i + 1] = pgm_read_byte(ssb_patch_content + offset + i);
    if (!command(bytes, sizeof bytes)) return false;
    delayMicroseconds(300);
  }
  delay(10);
  if (!getRevision()) return false;
  if (revision_.patch != 0xA902) return fail(Error::Patch);
  // AUDIOBW=1 (2.2kHz), SBCUTFLT=1, AVCEN=1, AFC disabled.
  return property(0x0101, 0x9011) && property(0x3302, 0);
}
bool Si473x::configure(Mode mode, uint8_t volume) {
  if (seeking_ && !cancelSeek()) return false;
  rds_.clear();
  const bool sameEngine = configured_ && (mode == mode_ || (isSsb(mode) && isSsb(mode_)));
  error_ = Error::None;
  if (!sameEngine) {
    configured_ = false; tuning_ = false; carrierKnown_ = false; signal_ = {};
    if (powered_) {
      const uint8_t down[] = {0x11};
      if (!command(down, 1)) return false;
      powered_ = false; delay(5);
    }
    if (isSsb(mode)) {
      if (!loadSsb()) return false;
    } else {
      if (!powerUp(mode == Mode::FM ? 0x10 : 0x11) || !getRevision()) return false;
      if (mode == Mode::FM) {
        if (!property(0x1100, 1) || !property(0x1102, 0) || !property(0x1502, 0x5501)) return false; // 50us FM de-emphasis
      } else if (!property(0x3102, 2) || !property(0x3302, 0)) return false; // AM 3kHz, soft mute off
    }
  }
  mode_ = mode; bandwidthHz_ = mode == Mode::FM ? 0 : isSsb(mode) ? 2200 : 3000; carrierKnown_ = false; confirmedHz_ = 0;
  if (!setVolume(volume) || !setMuted(false)) return false;
  configured_ = true;
  return true;
}
bool Si473x::setVolume(uint8_t volume) { return property(0x4000, volume > 63 ? 63 : volume); }
bool Si473x::setMuted(bool muted) { return property(0x4001, muted ? 3 : 0); }
bool Si473x::tune(uint32_t hz) {
  if (!configured_ || seeking_) return false;
  if (!clearRds()) return false;
  TuneWord word;
  if (!encodeFrequency(mode_, hz, word)) return fail(Error::Frequency);
  if (isSsb(mode_) && carrierKnown_) {
    // Keep the RF carrier anchored within an 8kHz BFO window, avoiding slow
    // carrier settling on every kHz boundary during continuous SSB tuning.
    const int64_t offset = int64_t(hz) - int64_t(carrier_) * 1000;
    if (offset >= -8000 && offset <= 8000) {
      if (!property(0x0100, static_cast<uint16_t>(-offset))) return false;
      requestedHz_ = confirmedHz_ = hz;
      return true;
    }
  }
  if (isSsb(mode_) && !property(0x0100, static_cast<uint16_t>(word.bfo))) return false;
  uint8_t bytes[] = {uint8_t(mode_ == Mode::FM ? 0x20 : 0x40),
                    uint8_t(isSsb(mode_) ? (mode_ == Mode::LSB ? 0x41 : 0x81) : 0),
                    uint8_t(word.carrier >> 8), uint8_t(word.carrier), 0, 0};
  // SW needs fixed antenna capacitor 1; LW/MW use automatic capacitor 0.
  if (mode_ != Mode::FM && hz >= 1800000) bytes[5] = 1;
  if (!command(bytes, mode_ == Mode::FM ? 5 : 6)) return false;
  carrier_ = word.carrier; requestedHz_ = hz;
  tuneStarted_ = lastPoll_ = millis(); tuning_ = true; carrierKnown_ = false;
  return true;
}
bool Si473x::seek(bool up, uint32_t lowerHz, uint32_t upperHz, uint32_t spacingHz) {
  if (!configured_ || tuning_ || seeking_ || isSsb(mode_)) return false;
  if (!clearRds()) return false;
  TuneWord low, high;
  if (lowerHz > upperHz || !encodeFrequency(mode_, lowerHz, low) || !encodeFrequency(mode_, upperHz, high))
    return fail(Error::Frequency);
  const uint16_t base = mode_ == Mode::FM ? 0x1400 : 0x3400;
  const uint16_t spacing = mode_ == Mode::FM ? (spacingHz <= 50000 ? 5 : spacingHz <= 100000 ? 10 : 20)
                                            : uint16_t(constrain(spacingHz / 1000, 1u, 10u));
  if (!property(base, low.carrier) || !property(base + 1, high.carrier) ||
      !property(base + 2, spacing) || !property(base + 3, mode_ == Mode::FM ? 3 : 5) ||
      !property(base + 4, mode_ == Mode::FM ? 20 : 25)) return false;
  // Clear any previous STC before beginning the new search. WRAP stays zero.
  const uint8_t statusCmd[] = {uint8_t(mode_ == Mode::FM ? 0x22 : 0x42), 1}; uint8_t response[8];
  if (!command(statusCmd, sizeof statusCmd, response, sizeof response)) return false;
  const uint8_t bytes[] = {uint8_t(mode_ == Mode::FM ? 0x21 : 0x41), uint8_t(up ? 8 : 0), 0, 0, 0,
                           uint8_t(mode_ != Mode::FM && lowerHz >= 1800000 ? 1 : 0)};
  if (!command(bytes, mode_ == Mode::FM ? 2 : 6)) return false;
  seekLowerHz_ = lowerHz; seekUpperHz_ = upperHz;
  seeking_ = true; seekResult_ = SeekResult::Searching;
  tuneStarted_ = lastPoll_ = millis();
  return true;
}
bool Si473x::seekStatus(bool cancel, bool acknowledge) {
  const uint8_t cmd[] = {uint8_t(mode_ == Mode::FM ? 0x22 : 0x42), uint8_t((cancel ? 2 : 0) | (acknowledge ? 1 : 0))};
  uint8_t response[8];
  if (!command(cmd, sizeof cmd, response, sizeof response)) return false;
  const uint16_t actual = (response[2] << 8) | response[3];
  const uint32_t hz = actual * (mode_ == Mode::FM ? 10000u : 1000u);
  if (hz < seekLowerHz_ || hz > seekUpperHz_) return fail(Error::Frequency);
  carrier_ = actual; requestedHz_ = confirmedHz_ = hz; carrierKnown_ = true;
  signal_ = {response[4], response[5], bool(response[1] & 1), false, 0, 0, false, false, false, false, false};
  if (cancel || (response[0] & 1)) {
    seeking_ = false;
    if (!clearRds()) return false;
    seekResult_ = cancel ? SeekResult::Cancelled : (response[1] & 0x80) ? SeekResult::Limit : SeekResult::Found;
  }
  return true;
}
bool Si473x::cancelSeek() {
  return !seeking_ || seekStatus(true, true);
}
bool Si473x::pollSeek() {
  if (millis() - lastPoll_ < 100) return true;
  lastPoll_ = millis();
  if (lastPoll_ - tuneStarted_ >= 30000) {
    if (!cancelSeek()) return false;
    seekResult_ = SeekResult::Timeout;
    return true;
  }
  if (!seekStatus(false, false)) return false;
  if (!seeking_) {
    const uint8_t ack[] = {uint8_t(mode_ == Mode::FM ? 0x22 : 0x42), 1};
    if (!command(ack, sizeof ack)) return false;
  }
  return true;
}
bool Si473x::poll() {
  if (seeking_) return pollSeek();
  if (!tuning_ || millis() - lastPoll_ < 5) return error_ == Error::None;
  lastPoll_ = millis();
  if (lastPoll_ - tuneStarted_ > 3000) return fail(Error::Timeout);
  uint8_t status; const uint8_t interrupt[] = {0x14};
  if (!command(interrupt, 1, &status, 1)) return false;
  if (!(status & 1)) return true;
  const uint8_t cmd[] = {uint8_t(mode_ == Mode::FM ? 0x22 : 0x42), 1}; uint8_t response[8];
  if (!command(cmd, sizeof cmd, response, sizeof response)) return false;
  const uint16_t actual = (response[2] << 8) | response[3];
  if (actual != carrier_) return fail(Error::Frequency);
  if (!clearRds()) return false;
  confirmedHz_ = requestedHz_; tuning_ = false; carrierKnown_ = true;
  signal_ = {response[4], response[5], bool(response[1] & 1), false, 0, 0, false, false, false, false, false};
  return true;
}
bool Si473x::setReception(const ReceptionSettings& settings) {
  int count; const auto* options = bandwidths(mode_, count);
  if (!configured_ || tuning_ || seeking_ || settings.bandwidth >= count || settings.gain < -1 ||
      settings.gain > maxGainIndex(mode_) || settings.softMuteDb > 31) return false;
  const auto& bw = options[settings.bandwidth];
  const uint16_t bwValue = isSsb(mode_) ? uint16_t(0x9000 | (bw.hz > 2000 ? 0x10 : 0) | bw.code) : bw.code;
  if (!property(mode_ == Mode::FM ? 0x1102 : isSsb(mode_) ? 0x0101 : 0x3102, bwValue) ||
      !property(mode_ == Mode::FM ? 0x1302 : 0x3302, settings.softMuteDb)) return false;
  const uint8_t agc[] = {uint8_t(mode_ == Mode::FM ? 0x28 : 0x48), uint8_t(settings.gain >= 0),
                         uint8_t(settings.gain < 0 ? 0 : settings.gain)};
  if (!command(agc, sizeof agc)) return false;
  bandwidthHz_ = bw.hz; signal_.qualityKnown = false;
  return true;
}
bool Si473x::clearRds() {
  rds_.clear();
  if (mode_ != Mode::FM || !configured_) return true;
  const uint8_t cmd[] = {0x24, 3}; // INTACK + MTFIFO; discard previous station data.
  return command(cmd, sizeof cmd);
}
bool Si473x::readRds() {
  if (!configured_ || mode_ != Mode::FM || tuning_ || seeking_) return true;
  uint8_t response[13]; const uint8_t peek[] = {0x24, 5}; // STATUSONLY + INTACK.
  if (!command(peek, sizeof peek, response, sizeof response)) return false;
  if (!(response[2] & 1) || (response[1] & 2) || (response[2] & 4)) {
    return clearRds(); // Sync loss or FIFO overrun: don't combine incomplete names.
  }
  const uint8_t count = response[3] < 4 ? response[3] : 4;
  const uint8_t pop[] = {0x24, 1};
  for (uint8_t i = 0; i < count; ++i) {
    if (!command(pop, sizeof pop, response, sizeof response)) return false;
    if (!(response[2] & 1) || (response[1] & 2)) return clearRds();
    rds_.accept(uint16_t(response[4] << 8 | response[5]), uint16_t(response[6] << 8 | response[7]),
               uint16_t(response[10] << 8 | response[11]), response[12], uint16_t(response[8] << 8 | response[9]));
  }
  return true;
}
bool Si473x::readSignal() {
  if (!configured_ || tuning_ || seeking_) return false;
  const uint8_t cmd[] = {uint8_t(mode_ == Mode::FM ? 0x23 : 0x43), 0}; uint8_t response[8];
  if (!command(cmd, sizeof cmd, response, mode_ == Mode::FM ? 8 : 6)) return false;
  signal_.rssi = response[4]; signal_.snr = response[5]; signal_.valid = response[2] & 1;
  signal_.blend = mode_ == Mode::FM ? (response[3] & 0x7f) : 0;
  signal_.stereo = mode_ == Mode::FM && signal_.blend > 0;
  signal_.pilot = mode_ == Mode::FM && (response[3] & 0x80);
  signal_.softMuted = response[2] & 8; signal_.afcRail = response[2] & 2;
  signal_.qualityKnown = false;
  const uint8_t agcCmd[] = {uint8_t(mode_ == Mode::FM ? 0x27 : 0x47)}; uint8_t agc[3];
  if (!command(agcCmd, sizeof agcCmd, agc, sizeof agc)) return false;
  signal_.agcEnabled = !(agc[1] & 1); signal_.gainIndex = agc[2]; signal_.qualityKnown = true;
  return true;
}
}

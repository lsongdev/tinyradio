#include "saved_settings.h"
namespace radio {
namespace {
void put32(uint8_t* p, uint32_t value) {
  for (int i = 0; i < 4; ++i) p[i] = value >> (i * 8);
}
uint32_t get32(const uint8_t* p) {
  uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value |= uint32_t(p[i]) << (i * 8);
  return value;
}
uint32_t crc(const SettingsBytes& bytes) {
  uint32_t value = 0xffffffff;
  for (size_t i = 0; i < bytes.size() - 4; ++i) {
    value ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  }
  return ~value;
}
bool valid(const SavedSettings& settings) {
  Model model;
  if (settings.volume > 63 || settings.brightness < 8 || !model.restore(settings.tuning)) return false;
  for (int i = 0; i < 4; ++i) {
    const auto mode = static_cast<drivers::Mode>(i); int count; drivers::bandwidths(mode, count);
    const auto& r = settings.reception[i];
    if (r.bandwidth >= count || r.gain < -1 || r.gain > drivers::maxGainIndex(mode) || r.softMuteDb > 31) return false;
  }
  return true;
}
}
bool encodeSettings(const SavedSettings& settings, SettingsBytes& bytes) {
  if (!valid(settings)) return false;
  bytes.fill(0); bytes[0] = 'A'; bytes[1] = 'T'; bytes[2] = 'S'; bytes[3] = 'M';
  bytes[4] = 1; bytes[5] = bandCount; bytes[6] = 4; bytes[7] = settings.tuning.band;
  bytes[8] = settings.volume; bytes[9] = settings.brightness; bytes[10] = settings.muted;
  for (size_t i = 0; i < bandCount; ++i) {
    auto* p = bytes.data() + 16 + i * 12; const auto& memory = settings.tuning.memories[i];
    p[0] = static_cast<uint8_t>(memory.mode); put32(p + 4, memory.frequencyHz); put32(p + 8, memory.stepHz);
  }
  for (int i = 0; i < 4; ++i) {
    auto* p = bytes.data() + 16 + bandCount * 12 + i * 4; const auto& r = settings.reception[i];
    p[0] = r.bandwidth; p[1] = r.gain + 1; p[2] = r.softMuteDb;
  }
  put32(bytes.data() + bytes.size() - 4, crc(bytes)); return true;
}
bool decodeSettings(const SettingsBytes& bytes, SavedSettings& settings) {
  if (bytes[0] != 'A' || bytes[1] != 'T' || bytes[2] != 'S' || bytes[3] != 'M' || bytes[4] != 1 ||
      bytes[5] != bandCount || bytes[6] != 4 || bytes[10] > 1 ||
      get32(bytes.data() + bytes.size() - 4) != crc(bytes)) return false;
  SavedSettings candidate;
  candidate.tuning.band = bytes[7]; candidate.volume = bytes[8]; candidate.brightness = bytes[9]; candidate.muted = bytes[10];
  for (size_t i = 0; i < bandCount; ++i) {
    const auto* p = bytes.data() + 16 + i * 12;
    candidate.tuning.memories[i] = {get32(p + 4), static_cast<Mode>(p[0]), get32(p + 8)};
  }
  for (int i = 0; i < 4; ++i) {
    const auto* p = bytes.data() + 16 + bandCount * 12 + i * 4;
    if (p[1] > 38) return false;
    candidate.reception[i] = {p[0], static_cast<int8_t>(int(p[1]) - 1), p[2]};
  }
  if (!valid(candidate)) return false;
  settings = candidate; return true;
}
}

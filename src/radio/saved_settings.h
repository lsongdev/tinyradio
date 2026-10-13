#pragma once
#include "model.h"
#include "drivers/si473x/settings.h"
#include <array>
namespace radio {
struct SavedSettings {
  Model::State tuning = Model().state();
  uint8_t volume = 18, brightness = 128;
  bool muted = false;
  drivers::ReceptionSettings reception[4] = {{0,-1,16}, {4,-1,0}, {3,-1,0}, {3,-1,0}};
};
// Explicit, versioned bytes; never persist C++ struct padding or enum layout.
using SettingsBytes = std::array<uint8_t, 16 + bandCount * 12 + 4 * 4 + 4>;
bool encodeSettings(const SavedSettings& settings, SettingsBytes& bytes);
bool decodeSettings(const SettingsBytes& bytes, SavedSettings& settings);
}

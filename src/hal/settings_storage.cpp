#include "settings_storage.h"
namespace hal {
bool SettingsStorage::begin() { return ready_ = preferences_.begin("ats-mini", false); }
bool SettingsStorage::load(void* data, size_t size) {
  return ready_ && preferences_.getBytesLength("settings") == size && preferences_.getBytes("settings", data, size) == size;
}
bool SettingsStorage::save(const void* data, size_t size) {
  return ready_ && preferences_.putBytes("settings", data, size) == size;
}
}

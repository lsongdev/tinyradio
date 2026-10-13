#pragma once
#include <Preferences.h>
namespace hal {
class SettingsStorage {
public:
  bool begin();
  bool load(void* data, size_t size);
  bool save(const void* data, size_t size);
private:
  Preferences preferences_;
  bool ready_ = false;
};
}

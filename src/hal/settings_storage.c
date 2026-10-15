#include "settings_storage.h"
static const char key[] = "settings";
bool hal_settings_begin(hal_settings_storage_t* storage, const char* name) {
  return storage->ready = nvs_open(name, NVS_READWRITE, &storage->handle) == ESP_OK;
}
bool hal_settings_load(hal_settings_storage_t* storage, void* data, size_t size) {
  size_t stored = 0;
  return storage->ready && nvs_get_blob(storage->handle, key, NULL, &stored) == ESP_OK && stored == size &&
         nvs_get_blob(storage->handle, key, data, &stored) == ESP_OK && stored == size;
}
bool hal_settings_save(hal_settings_storage_t* storage, const void* data, size_t size) {
  return storage->ready && nvs_set_blob(storage->handle, key, data, size) == ESP_OK &&
         nvs_commit(storage->handle) == ESP_OK;
}

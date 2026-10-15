#pragma once
#include <nvs.h>
#include <stdbool.h>
#include <stddef.h>
// One versioned settings blob under key "settings" in a board-owned namespace.
// The namespace is part of the stored format: changing it abandons saved settings.
typedef struct {
  nvs_handle_t handle;
  bool ready;
} hal_settings_storage_t;
bool hal_settings_begin(hal_settings_storage_t* storage, const char* name);
// Succeeds only when the stored blob has exactly `size` bytes.
bool hal_settings_load(hal_settings_storage_t* storage, void* data, size_t size);
bool hal_settings_save(hal_settings_storage_t* storage, const void* data, size_t size);

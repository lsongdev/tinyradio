#pragma once
#include <nvs.h>
#include <stdbool.h>
#include <stddef.h>
// Versioned blobs in a board-owned NVS namespace. The namespace and keys are
// part of the stored format: changing them abandons saved data.
typedef struct {
  nvs_handle_t handle;
  bool ready;
} hal_settings_storage_t;
bool hal_settings_begin(hal_settings_storage_t* storage, const char* name);
// Succeeds only when the stored blob has exactly `size` bytes.
bool hal_settings_load(hal_settings_storage_t* storage, const char* key, void* data, size_t size);
bool hal_settings_save(hal_settings_storage_t* storage, const char* key, const void* data, size_t size);

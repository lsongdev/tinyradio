#pragma once
#include <stdint.h>

// IDs are persisted: append new themes without reordering existing entries.
typedef enum {
  UI_THEME_DARK, UI_THEME_LIGHT, UI_THEME_PARCHMENT, UI_THEME_MIDNIGHT, UI_THEME_FOREST, UI_THEME_COUNT
} ui_theme_id_t;

// Semantic colors, encoded as RGB565. All theme values live in theme.c.
typedef struct {
  uint16_t background, text_primary, text_secondary, text_muted, border_subtle;
  // Theme accent: enabled features, current choices and spectrum bars.
  uint16_t accent, control_focus, control_disabled;
  uint16_t status_positive, status_danger, status_notice;
  uint16_t audio_muted, active; // active emphasizes feature-specific states; boxed controls change only text.
  uint16_t ruler_current, ruler_adjacent;
} ui_theme_t;

#ifdef __cplusplus
extern "C" {
#endif
const ui_theme_t* ui_theme_get(ui_theme_id_t theme);
const char* ui_theme_name(ui_theme_id_t theme);
#ifdef __cplusplus
}
#endif

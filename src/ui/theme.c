#include "theme.h"

// RGB565 palettes. In light mode, colored foregrounds are darker for contrast.
// accent sets the overall style of controls and the spectrum.
// active is shared by seek mode, band lock, soft mute and external power.
static const ui_theme_t dark = {
  .background = 0x0000,
  .text_primary = 0xFFFF,
  .text_secondary = 0xD69A,
  .text_muted = 0x7BEF,
  .border_subtle = 0x7BEF,
  .accent = 0x07FF,
  .control_focus = 0xFFE0,
  .control_disabled = 0x7BEF,
  .status_positive = 0x07E0,
  .status_danger = 0xF800,
  .status_notice = 0xFFE0,
  .audio_muted = 0xF800,
  .audio_stereo = 0x07FF,
  .active = 0xF81F,
  .ruler_current = 0xF800,
  .ruler_adjacent = 0x07FF,
};

static const ui_theme_t light = {
  .background = 0xFFFF,
  .text_primary = 0x1082,
  .text_secondary = 0x4208,
  .text_muted = 0x6B4D,
  .border_subtle = 0xC618,
  .accent = 0x0410,
  .control_focus = 0x8B60,
  .control_disabled = 0x8C51,
  .status_positive = 0x03C0,
  .status_danger = 0xB800,
  .status_notice = 0x8B60,
  .audio_muted = 0xB800,
  .audio_stereo = 0x0410,
  .active = 0x5018,
  .ruler_current = 0xB800,
  .ruler_adjacent = 0x0410,
};

const ui_theme_t* ui_theme_get(ui_theme_id_t theme) {
  return theme == UI_THEME_LIGHT ? &light : &dark;
}

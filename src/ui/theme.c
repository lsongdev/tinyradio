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
  // Cool paper background; even muted/disabled text keeps >= 4.5:1 contrast.
  .background = 0xFFDF,
  .text_primary = 0x08A5,
  .text_secondary = 0x320A,
  .text_muted = 0x42AD,
  .border_subtle = 0x7C53,
  .accent = 0x034E,
  .control_focus = 0x8A40,
  .control_disabled = 0x63B1,
  .status_positive = 0x1326,
  .status_danger = 0xB8E3,
  .status_notice = 0x8A40,
  .audio_muted = 0xB8E3,
  .audio_stereo = 0x034E,
  .active = 0x6915,
  .ruler_current = 0xB8E3,
  .ruler_adjacent = 0x034E,
};

const ui_theme_t* ui_theme_get(ui_theme_id_t theme) {
  return theme == UI_THEME_LIGHT ? &light : &dark;
}

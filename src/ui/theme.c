#include "theme.h"

// Quantize from readable sRGB values at compile time; the panel consumes RGB565.
#define RGB(hex) ((((hex) >> 8) & 0xF800) | (((hex) >> 5) & 0x07E0) | (((hex) >> 3) & 0x001F))

// Normal controls stay neutral; accent marks enabled/current options, focus
// marks encoder ownership, and active marks a feature currently taking effect.
static const struct {
  const char* name;
  ui_theme_t colors;
} themes[UI_THEME_COUNT] = {
  {"Dark", {
    .background = RGB(0x000000),
    .text_primary = RGB(0xFFFFFF),
    .text_secondary = RGB(0xD0D4D8),
    .text_muted = RGB(0x92989E),
    .border_subtle = RGB(0x747B83),
    .accent = RGB(0x00FFFF),
    .control_focus = RGB(0xFFFF00),
    .control_disabled = RGB(0x92989E),
    .status_positive = RGB(0x00FF00),
    .status_danger = RGB(0xFF6060),
    .status_notice = RGB(0xFFFF00),
    .audio_muted = RGB(0xFF6060),
    .active = RGB(0xFF60FF),
    .ruler_current = RGB(0xFF6060),
    .ruler_adjacent = RGB(0x00FFFF),
  }},
  {"Paper", {
    .background = RGB(0xF7F8FA),
    .text_primary = RGB(0x101820),
    .text_secondary = RGB(0x303840),
    .text_muted = RGB(0x505860),
    .border_subtle = RGB(0x707880),
    .accent = RGB(0x173B70),
    .control_focus = RGB(0x803600),
    .control_disabled = RGB(0x606870),
    .status_positive = RGB(0x14502C),
    .status_danger = RGB(0x991F30),
    .status_notice = RGB(0x803600),
    .audio_muted = RGB(0x991F30),
    .active = RGB(0x6C2868),
    .ruler_current = RGB(0x991F30),
    .ruler_adjacent = RGB(0x173B70),
  }},
  {"Parchment", {
    .background = RGB(0xF3E9D5),
    .text_primary = RGB(0x231C18),
    .text_secondary = RGB(0x453B32),
    .text_muted = RGB(0x60564A),
    .border_subtle = RGB(0x84786A),
    .accent = RGB(0x623F26),
    .control_focus = RGB(0x254F70),
    .control_disabled = RGB(0x6A5E50),
    .status_positive = RGB(0x2C5032),
    .status_danger = RGB(0x902B25),
    .status_notice = RGB(0x71430D),
    .audio_muted = RGB(0x902B25),
    .active = RGB(0x702E58),
    .ruler_current = RGB(0x902B25),
    .ruler_adjacent = RGB(0x623F26),
  }},
  {"Midnight", {
    .background = RGB(0x0C1628),
    .text_primary = RGB(0xE8EFFA),
    .text_secondary = RGB(0xB8C8DF),
    .text_muted = RGB(0x8B9DB8),
    .border_subtle = RGB(0x637C9A),
    .accent = RGB(0x91BBFF),
    .control_focus = RGB(0xFFCE80),
    .control_disabled = RGB(0x8B9DB8),
    .status_positive = RGB(0x7AD9AF),
    .status_danger = RGB(0xFF929D),
    .status_notice = RGB(0xFFCE80),
    .audio_muted = RGB(0xFF929D),
    .active = RGB(0xE1A6FF),
    .ruler_current = RGB(0xFF929D),
    .ruler_adjacent = RGB(0x91BBFF),
  }},
  {"Forest", {
    .background = RGB(0x101D19),
    .text_primary = RGB(0xE7F0E8),
    .text_secondary = RGB(0xB7CEC0),
    .text_muted = RGB(0x90AC9B),
    .border_subtle = RGB(0x608472),
    .accent = RGB(0x94D7B0),
    .control_focus = RGB(0xF3CB82),
    .control_disabled = RGB(0x90AC9B),
    .status_positive = RGB(0x94D7B0),
    .status_danger = RGB(0xFF9D94),
    .status_notice = RGB(0xF3CB82),
    .audio_muted = RGB(0xFF9D94),
    .active = RGB(0xD3ACF3),
    .ruler_current = RGB(0xFF9D94),
    .ruler_adjacent = RGB(0x94D7B0),
  }},
};

static unsigned theme_index(ui_theme_id_t theme) {
  return (unsigned)theme < UI_THEME_COUNT ? (unsigned)theme : UI_THEME_DARK;
}
const ui_theme_t* ui_theme_get(ui_theme_id_t theme) {
  return &themes[theme_index(theme)].colors;
}
const char* ui_theme_name(ui_theme_id_t theme) {
  return themes[theme_index(theme)].name;
}

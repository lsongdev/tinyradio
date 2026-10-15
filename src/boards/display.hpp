#pragma once
#include <LovyanGFX.hpp>
// The selected board's panel, for the C++ UI layer. Every board's display.cpp
// defines this; board_display_begin() must succeed before drawing.
lgfx::LGFX_Device& board_display_device();

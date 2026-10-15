#pragma once
// Exactly one board is compiled: platformio.ini defines BOARD_<NAME> and filters
// the other board directories out of the build. Every board.h provides the same
// C API (board_*() and the BOARD_* constants); see docs/development.md.
#if defined(BOARD_ATS_MINI)
#include "ats_mini/board.h"
#elif defined(BOARD_V5A)
#include "v5a/board.h"
#else
#error "No board selected: add -D BOARD_<NAME> to the PlatformIO environment"
#endif

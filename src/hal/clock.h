#pragma once
#include <stdint.h>
#include <esp_timer.h>
namespace hal {
inline uint64_t monotonicMilliseconds() { return uint64_t(esp_timer_get_time()) / 1000; }
}

#pragma once
#include <esp_timer.h>
#include <stdint.h>
// 64-bit monotonic time since boot; does not wrap like millis().
static inline uint64_t hal_monotonic_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

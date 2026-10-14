#pragma once
#include "drivers/si473x/clock_time.h"
namespace radio {
struct ClockReading {
  bool valid = false;
  int32_t localMjd = 0;
  uint16_t minutes = 0;
};
// Presentation clock: limited holdover between the station's minute updates.
// No RTC, system clock, timezone override, or persisted time is involved.
class Clock {
public:
  static constexpr uint32_t maxAgeMs = 5 * 60 * 1000;
  void clear() { *this = Clock(); }
  void update(const drivers::RdsClockTime& sample, uint32_t now) {
    if (active_ && uint32_t(now - anchoredAt_) >= maxAgeMs) active_ = false;
    if (!sample.valid) { clear(); return; }
    if (seen_ && sample.pi == last_.pi && sample.sequence == last_.sequence) return;
    if (!seen_ || sample.pi != last_.pi || sample.mjd != last_.mjd || sample.utcMinutes != last_.utcMinutes ||
        sample.offsetMinutes != last_.offsetMinutes) {
      anchoredAt_ = now; active_ = true;
    }
    last_ = sample; seen_ = true;
  }
  ClockReading read(uint32_t now) const {
    ClockReading result;
    const uint32_t elapsed = now - anchoredAt_;
    if (!active_ || elapsed >= maxAgeMs) return result;
    int minutes = int(last_.utcMinutes) + last_.offsetMinutes + int(elapsed / 60000);
    result.localMjd = last_.mjd;
    while (minutes < 0) { minutes += 1440; --result.localMjd; }
    while (minutes >= 1440) { minutes -= 1440; ++result.localMjd; }
    result.valid = true; result.minutes = minutes; return result;
  }
private:
  drivers::RdsClockTime last_;
  uint32_t anchoredAt_ = 0;
  bool seen_ = false, active_ = false;
};
}

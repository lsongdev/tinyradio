#pragma once
#include <stdint.h>
namespace services {
enum class TimeSource : uint8_t { None, Rds, Ntp };
inline const char* timeSourceName(TimeSource source) {
  return source == TimeSource::Rds ? "RDS" : source == TimeSource::Ntp ? "NTP" : "NONE";
}
struct ClockReading {
  bool valid = false;
  int64_t utcSeconds = 0, localSeconds = 0;
  int16_t utcOffsetMinutes = 0;
  uint8_t hour = 0, minute = 0, second = 0;
  TimeSource source = TimeSource::None;
};
// A shared device clock. Sources submit Unix UTC seconds; timezone is separate.
// nowMs is a 64-bit monotonic time since boot, independent of radio state.
class Clock {
public:
  void synchronize(int64_t utcSeconds, TimeSource source, uint64_t nowMs) {
    if (source == TimeSource::None) return;
    utcSeconds_ = utcSeconds; synchronizedAt_ = nowMs; source_ = source;
  }
  void setUtcOffsetMinutes(int16_t offset) { utcOffsetMinutes_ = offset; }
  ClockReading read(uint64_t nowMs) const {
    ClockReading result;
    if (source_ == TimeSource::None || nowMs < synchronizedAt_) return result;
    result.valid = true; result.source = source_; result.utcOffsetMinutes = utcOffsetMinutes_;
    result.utcSeconds = utcSeconds_ + int64_t((nowMs - synchronizedAt_) / 1000);
    result.localSeconds = result.utcSeconds + int64_t(utcOffsetMinutes_) * 60;
    int64_t daySeconds = result.localSeconds % 86400;
    if (daySeconds < 0) daySeconds += 86400;
    result.hour = daySeconds / 3600; result.minute = daySeconds / 60 % 60; result.second = daySeconds % 60;
    return result;
  }
private:
  int64_t utcSeconds_ = 0;
  uint64_t synchronizedAt_ = 0;
  int16_t utcOffsetMinutes_ = 0;
  TimeSource source_ = TimeSource::None;
};
}

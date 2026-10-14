#pragma once
#include "drivers/si473x/clock_time.h"
#include "services/clock.h"
namespace app {
// Convert the vendor-specific CT sample to the shared UTC clock API here,
// keeping PI, group sequences, MJD and other RDS details out of the service.
class RdsClockSync {
public:
  void update(const drivers::RdsClockTime& sample, services::Clock& clock, uint64_t nowMs) {
    if (!sample.valid) return; // Reception loss/withdrawal does not unset device time.
    const bool newTime = !seen_ || sample.mjd != mjd_ || sample.utcMinutes != utcMinutes_;
    if (newTime) {
      const int64_t utcSeconds = (int64_t(sample.mjd) - 40587) * 86400 + int64_t(sample.utcMinutes) * 60;
      clock.synchronize(utcSeconds, services::TimeSource::Rds, nowMs);
    }
    if (!seen_ || newTime || sample.offsetMinutes != offsetMinutes_)
      clock.setUtcOffsetMinutes(sample.offsetMinutes);
    // Deduplicate by broadcast content, including across tuning and PI changes.
    // Re-reading a minute or only changing its offset must not reset seconds.
    seen_ = true; mjd_ = sample.mjd; utcMinutes_ = sample.utcMinutes; offsetMinutes_ = sample.offsetMinutes;
  }
private:
  bool seen_ = false;
  uint32_t mjd_ = 0;
  uint16_t utcMinutes_ = 0;
  int16_t offsetMinutes_ = 0;
};
}

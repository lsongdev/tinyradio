#pragma once
#include <stdint.h>
namespace hal {
struct BatteryReading {
  uint32_t adcMillivolts;
  float volts;
  uint8_t estimatedPercent;
  bool externalPowerLikely;
  bool plausible;
};
class Battery {
public:
  void begin();
  BatteryReading read() const;
};
}

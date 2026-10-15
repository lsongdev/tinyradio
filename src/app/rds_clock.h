#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "drivers/si473x/clock_time.h"
#include "services/device_clock.h"
// Converts the RDS CT sample to the shared UTC clock API here, keeping PI,
// group sequences, MJD and other RDS details out of the clock service.
typedef struct {
  bool seen;
  uint32_t mjd;
  uint16_t utc_minutes;
  int16_t offset_minutes;
} rds_clock_sync_t;
// Returns true when it changed the clock's time or offset.
bool rds_clock_sync_update(rds_clock_sync_t* sync, const rds_clock_time_t* sample, device_clock_t* clock, uint64_t now_ms);

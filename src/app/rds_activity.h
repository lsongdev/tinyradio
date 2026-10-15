#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "drivers/si473x/si473x.h"

// Coalesce incoming groups into short pulses with a visible gap between them.
typedef struct {
  uint32_t groups, started_at;
  bool receiving, started;
} rds_activity_t;
void rds_activity_update(rds_activity_t* activity, const si473x_rds_status_t* status, bool available, uint32_t now);

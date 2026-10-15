#pragma once
#include "tuning.h"
typedef struct { uint32_t hz; uint8_t code; } si473x_bandwidth_t;
static inline const si473x_bandwidth_t* si473x_bandwidths(si473x_mode_t mode, int* count) {
  static const si473x_bandwidth_t fm[] = {{0,0},{40000,4},{60000,3},{84000,2},{110000,1}};
  static const si473x_bandwidth_t am[] = {{1000,4},{1800,5},{2000,3},{2500,6},{3000,2},{4000,1},{6000,0}};
  static const si473x_bandwidth_t ssb[] = {{500,4},{1000,5},{1200,0},{2200,1},{3000,2},{4000,3}};
  *count = mode == SI473X_FM ? 5 : si473x_is_ssb(mode) ? 6 : 7;
  return mode == SI473X_FM ? fm : si473x_is_ssb(mode) ? ssb : am;
}
typedef struct {
  uint8_t bandwidth;    // Index into mode-specific, ascending bandwidth list.
  int8_t gain;          // -1: AGC; otherwise manual attenuation index, not dB.
  uint8_t soft_mute_db;
} si473x_reception_t;
static inline int si473x_max_gain_index(si473x_mode_t mode) { return mode == SI473X_FM ? 26 : 37; }

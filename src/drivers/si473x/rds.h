#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "enhanced_radiotext.h"
#include "clock_time.h"
// Decode PS/RT/eRT with message confirmation, and complete 4A clock-time groups.
typedef struct {
  rds_ert_t ert;
  rds_clock_time_t clock;
  uint16_t pi;
  bool pi_known;
  // PS: groups 0A/0B.
  uint8_t mask;
  bool previous_known;
  char candidate[8], previous[8], published[9];
  // RT: groups 2A/2B.
  uint16_t rt_mask;
  int rt_previous_length;
  bool rt_known, rt_flag, rt_version_b, rt_previous_known;
  char rt_candidate[64], rt_previous[64], rt_published[65];
} rds_decoder_t;
void rds_decoder_clear(rds_decoder_t* rds);
// errors holds the SI473x BLE fields for blocks A..D in bits 7..0.
void rds_decoder_accept(rds_decoder_t* rds, uint16_t pi, uint16_t b, uint16_t d, uint8_t errors, uint16_t c);
static inline const char* rds_ps(const rds_decoder_t* rds) { return rds->published; }
static inline const char* rds_radio_text(const rds_decoder_t* rds) { return rds->rt_published; }
static inline const char* rds_enhanced_text(const rds_decoder_t* rds) { return rds->ert.published; }
static inline int rds_enhanced_group(const rds_decoder_t* rds) { return rds->ert.group; }
static inline uint16_t rds_enhanced_flags(const rds_decoder_t* rds) { return rds->ert.flags; }
static inline const rds_clock_time_t* rds_clock_time(const rds_decoder_t* rds) { return &rds->clock; }

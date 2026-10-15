#pragma once
#include <stdbool.h>
#include <stdint.h>
// eRT ODA 0x6552 on the ordinary RDS stream. A 3A announcement assigns its
// carrier group; group-B variants cannot carry the two text blocks required.
typedef struct {
  int group;        // Five-bit type/version code, or -1.
  uint16_t flags;
  bool active, previous_known;
  uint8_t next;
  int previous_length, previous_end;
  uint8_t candidate[128], previous[128];
  char published[129];
} rds_ert_t;
void rds_ert_clear(rds_ert_t* ert);
void rds_ert_announce(rds_ert_t* ert, uint8_t group, uint16_t flags, uint16_t aid);
static inline bool rds_ert_matches(const rds_ert_t* ert, uint8_t group) { return ert->group >= 0 && group == ert->group; }
void rds_ert_accept(rds_ert_t* ert, uint8_t segment, uint16_t c, uint16_t d);

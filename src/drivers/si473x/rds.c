#include "rds.h"
#include <string.h>

static char printable(uint8_t c) { return c >= 32 && c <= 126 ? (char)c : '?'; }
static void accept_text(rds_decoder_t* r, uint16_t b, uint16_t d, uint16_t c, uint8_t errors) {
  const bool version_b = b & 0x800, flag = b & 0x10;
  if (!r->rt_known || flag != r->rt_flag || version_b != r->rt_version_b) {
    r->rt_mask = 0; r->rt_previous_known = false; r->rt_published[0] = 0;
    memset(r->rt_candidate, ' ', sizeof r->rt_candidate);
    r->rt_known = true; r->rt_flag = flag; r->rt_version_b = version_b;
  }
  if (!version_b && ((errors >> 2) & 3) > 2) return;
  const int segment = b & 15, width = version_b ? 2 : 4, capacity = version_b ? 32 : 64;
  const uint8_t bytes[] = {(uint8_t)(c >> 8), (uint8_t)c, (uint8_t)(d >> 8), (uint8_t)d};
  for (int i = 0; i < width; ++i) {
    const uint8_t value = bytes[i + (version_b ? 2 : 0)];
    r->rt_candidate[segment * width + i] = value == 13 ? '\r' : printable(value);
  }
  r->rt_mask |= (uint16_t)(1u << segment);
  int length = capacity;
  for (int i = 0; i < capacity; ++i) if (r->rt_candidate[i] == '\r') { length = i; break; }
  const int last = length < capacity ? length / width : 15;
  const uint16_t required = (uint16_t)((1u << (last + 1)) - 1);
  if ((r->rt_mask & required) != required) return;
  if (r->rt_previous_known && r->rt_previous_length == length && !memcmp(r->rt_previous, r->rt_candidate, length)) {
    memcpy(r->rt_published, r->rt_candidate, length); r->rt_published[length] = 0;
    for (int i = length - 1; i >= 0 && r->rt_published[i] == ' '; --i) r->rt_published[i] = 0;
  }
  memcpy(r->rt_previous, r->rt_candidate, length); r->rt_previous_length = length;
  r->rt_previous_known = true; r->rt_mask = 0;
}
void rds_decoder_clear(rds_decoder_t* r) {
  memset(r, 0, sizeof *r);
  rds_ert_clear(&r->ert);
}
void rds_decoder_accept(rds_decoder_t* r, uint16_t pi, uint16_t b, uint16_t d, uint8_t errors, uint16_t c) {
  if ((errors >> 6) > 2) return;
  if (!r->pi_known || pi != r->pi) { rds_decoder_clear(r); r->pi = pi; r->pi_known = true; }
  if (((errors >> 4) & 3) > 2) return;
  const bool c_and_d = ((errors >> 2) & 3) <= 2 && (errors & 3) <= 2;
  const uint8_t group = (b >> 11) & 31;
  if (group == 8) { // 4A only; 4B is an unrelated ODA carrier.
    if (c_and_d) rds_decode_clock_time(pi, b, c, d, &r->clock);
    return;
  }
  if (group == 6) {
    if (c_and_d) rds_ert_announce(&r->ert, b & 31, c, d);
    return;
  }
  if (rds_ert_matches(&r->ert, group)) {
    if (c_and_d) rds_ert_accept(&r->ert, b & 31, c, d);
    return;
  }
  if ((errors & 3) > 2) return;
  if ((b >> 12) == 2) { accept_text(r, b, d, c, errors); return; }
  if ((b >> 12) != 0) return;
  const uint8_t segment = b & 3;
  r->candidate[segment * 2] = printable(d >> 8);
  r->candidate[segment * 2 + 1] = printable(d & 255);
  r->mask |= 1 << segment;
  if (r->mask != 15) return;
  if (r->previous_known && !memcmp(r->previous, r->candidate, 8)) {
    memcpy(r->published, r->candidate, 8);
    for (int i = 7; i >= 0 && r->published[i] == ' '; --i) r->published[i] = 0;
  }
  memcpy(r->previous, r->candidate, 8); r->previous_known = true; r->mask = 0;
}

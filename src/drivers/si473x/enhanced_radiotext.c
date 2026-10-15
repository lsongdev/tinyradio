#include "enhanced_radiotext.h"
#include <string.h>

static bool valid_utf8(const uint8_t* bytes, int length) {
  for (int i = 0; i < length;) {
    uint32_t code = bytes[i++], minimum = 0; int following = 0;
    if (code < 0x80) {
      if (code < 0x20 || code == 0x7f) return false;
      continue;
    }
    if (code >= 0xc2 && code <= 0xdf) { code &= 0x1f; following = 1; minimum = 0x80; }
    else if (code >= 0xe0 && code <= 0xef) { code &= 0x0f; following = 2; minimum = 0x800; }
    else if (code >= 0xf0 && code <= 0xf4) { code &= 7; following = 3; minimum = 0x10000; }
    else return false;
    if (following > length - i) return false;
    while (following--) {
      const uint8_t next = bytes[i++];
      if ((next & 0xc0) != 0x80) return false;
      code = (code << 6) | (next & 0x3f);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
  }
  return true;
}
void rds_ert_clear(rds_ert_t* ert) {
  memset(ert, 0, sizeof *ert);
  ert->group = -1;
}
void rds_ert_announce(rds_ert_t* ert, uint8_t group, uint16_t flags, uint16_t aid) {
  if (aid != 0x6552) {
    if (group == ert->group) rds_ert_clear(ert); // This carrier was reassigned to another ODA.
    return;
  }
  // Only allocatable A groups: never hijack PS, RT, 3A, CT, EON or LPS.
  if ((group & 1) || group < 10 || group > 26) { rds_ert_clear(ert); return; }
  if (ert->group != group || ert->flags != flags) {
    rds_ert_clear(ert); ert->group = group; ert->flags = flags;
  }
}
void rds_ert_accept(rds_ert_t* ert, uint8_t segment, uint16_t c, uint16_t d) {
  // Bit 0 selects UTF-8 rather than legacy UCS-2; bit 1 requests RTL.
  if (!(ert->flags & 1) || (ert->flags & 2) || segment >= 32) return;
  const uint8_t bytes[] = {(uint8_t)(c >> 8), (uint8_t)c, (uint8_t)(d >> 8), (uint8_t)d};
  const int offset = segment * 4;
  if (segment == 0) {
    // Consecutive identical retransmissions of a segment are harmless.
    if (ert->active && ert->next == 1 && !memcmp(ert->candidate, bytes, 4)) return;
    memset(ert->candidate, 0, sizeof ert->candidate); ert->active = true; ert->next = 0;
  }
  if (!ert->active) return;
  if (segment != ert->next) {
    if (segment < ert->next && !memcmp(ert->candidate + offset, bytes, 4)) return;
    ert->active = false; ert->previous_known = false; return;
  }
  // There is no A/B bit. A changed valid segment invalidates old displayed
  // text; confirmation still requires two matching complete transmissions.
  if (ert->published[0] && ert->previous_known && offset < ert->previous_end) {
    const int count = ert->previous_end - offset < 4 ? ert->previous_end - offset : 4;
    if (memcmp(ert->previous + offset, bytes, count)) ert->published[0] = 0;
  }
  memcpy(ert->candidate + offset, bytes, 4); ++ert->next;
  int length = 128;
  for (int i = offset; i < offset + 4; ++i)
    if (ert->candidate[i] == 0x0d) { length = i; break; }
  if (length == 128 && ert->next != 32) return;
  ert->active = false;
  if (!valid_utf8(ert->candidate, length)) { ert->previous_known = false; return; }
  if (ert->previous_known && ert->previous_length == length && !memcmp(ert->previous, ert->candidate, length)) {
    memcpy(ert->published, ert->candidate, length); ert->published[length] = 0;
    for (int i = length - 1; i >= 0 && ert->published[i] == ' '; --i) ert->published[i] = 0;
  } else if (ert->published[0]) ert->published[0] = 0;
  memcpy(ert->previous, ert->candidate, sizeof ert->previous);
  ert->previous_length = length; ert->previous_end = length < 128 ? length + 1 : 128;
  ert->previous_known = true;
}

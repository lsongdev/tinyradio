#include "drivers/si473x/rds.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void announcement(rds_decoder_t* r, uint8_t group, uint16_t flags, uint16_t aid, uint8_t errors) {
  rds_decoder_accept(r, 0x1234, 0x3000 | group, aid, errors, flags);
}
static void announce(rds_decoder_t* r) { announcement(r, 22, 1, 0x6552, 0); }
static void packet(rds_decoder_t* r, uint8_t group, uint8_t segment, const uint8_t* bytes, uint8_t errors) {
  rds_decoder_accept(r, 0x1234, (uint16_t)(group << 11) | segment,
                     (uint16_t)(bytes[2] << 8 | bytes[3]), errors, (uint16_t)(bytes[0] << 8 | bytes[1]));
}
// Sends `length` bytes (which may include NUL) as one eRT cycle, 0x0D-terminated when shorter than 128.
static void send(rds_decoder_t* r, const char* text, size_t length, uint8_t group, int missing, uint8_t errors, bool duplicate) {
  assert(length <= 128); uint8_t data[128]; memset(data, 13, sizeof data);
  memcpy(data, text, length);
  const int count = length < 128 ? (int)(length / 4 + 1) : 32;
  for (int segment = 0; segment < count; ++segment) {
    if (segment == missing) continue;
    packet(r, group, segment, data + segment * 4, errors);
    if (duplicate) packet(r, group, segment, data + segment * 4, errors);
  }
}
static void message(rds_decoder_t* r, const char* text, uint8_t group) { send(r, text, strlen(text), group, -1, 0, false); }
static void twice(rds_decoder_t* r, const char* text) { message(r, text, 22); message(r, text, 22); }
static void confirm(rds_decoder_t* r, const char* text, uint8_t group) {
  message(r, text, group); message(r, text, group);
  assert(!strcmp(rds_enhanced_text(r), text));
}
static bool empty(const rds_decoder_t* r) { return !rds_enhanced_text(r)[0]; }
int main(void) {
  rds_decoder_t decoder, *r = &decoder; rds_decoder_clear(r);
  twice(r, "Not announced"); assert(empty(r));
  announcement(r, 22, 1, 0x6552, 0x0c); assert(rds_enhanced_group(r) == -1); // Bad config block.
  announcement(r, 22, 1, 0x6552, 3); assert(rds_enhanced_group(r) == -1); // Bad AID block.
  announcement(r, 22, 1, 0x6552, 0x30); assert(rds_enhanced_group(r) == -1);
  announce(r); assert(rds_enhanced_group(r) == 22 && rds_enhanced_flags(r) == 1);
  const char* chinese = "AB中文节目 - FM95.9";
  send(r, chinese, strlen(chinese), 22, -1, 0xaa, false); assert(empty(r));
  announce(r); // Repeated 3A announcements must not reset confirmation.
  send(r, chinese, strlen(chinese), 22, -1, 0xaa, false); assert(!strcmp(rds_enhanced_text(r), chinese));
  announcement(r, 24, 1, 0x4bd7, 0); assert(rds_enhanced_group(r) == 22); // Unrelated ODA.
  message(r, "Wrong group", 24); assert(!strcmp(rds_enhanced_text(r), chinese));
  message(r, "Updated 中文", 22); assert(empty(r));
  message(r, "Updated 中文", 22); assert(!strcmp(rds_enhanced_text(r), "Updated 中文"));
  twice(r, ""); assert(empty(r)); // Broadcast clears text.
  announcement(r, 24, 1, 0x6552, 0); assert(rds_enhanced_group(r) == 24);
  message(r, "Old group", 22); assert(empty(r)); confirm(r, "Dynamic group", 24);
  announcement(r, 24, 1, 0x4bd7, 0); assert(rds_enhanced_group(r) == -1 && empty(r));
  const uint16_t unsupported[] = {0, 3};
  for (int i = 0; i < 2; ++i) {
    announcement(r, 22, unsupported[i], 0x6552, 0); twice(r, "Unsupported"); assert(empty(r));
  }
  const uint8_t reserved[] = {0, 4, 6, 8, 23, 28, 30};
  for (size_t i = 0; i < sizeof reserved; ++i) {
    announcement(r, reserved[i], 1, 0x6552, 0); assert(rds_enhanced_group(r) == -1);
  }
  announce(r);
  const char* gap = "Missing middle segment 中文";
  send(r, gap, strlen(gap), 22, 1, 0, false); send(r, gap, strlen(gap), 22, 1, 0, false);
  assert(empty(r)); confirm(r, "Recovered", 22);
  rds_decoder_clear(r); announce(r);
  const uint8_t late[] = {'L','A','T','E'}; packet(r, 22, 1, late, 0); assert(empty(r));
  const char* repeated = "Repeated packets 中文";
  send(r, repeated, strlen(repeated), 22, -1, 0, true); send(r, repeated, strlen(repeated), 22, -1, 0, true);
  assert(!strcmp(rds_enhanced_text(r), repeated));
  const uint8_t corrupt[] = {0xc0, 0x30, 0x0c, 3};
  for (size_t i = 0; i < sizeof corrupt; ++i) {
    rds_decoder_clear(r); announce(r);
    send(r, "Corrupt", 7, 22, -1, corrupt[i], false); send(r, "Corrupt", 7, 22, -1, corrupt[i], false);
    assert(empty(r));
  }
  const char* invalid[] = {"\xc0\xaf", "\xe4\xb8", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe4X\xad", "\x80", "\x1b"};
  for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
    rds_decoder_clear(r); announce(r); twice(r, invalid[i]); assert(empty(r));
  }
  rds_decoder_clear(r); announce(r);
  send(r, "A\0B", 3, 22, -1, 0, false); send(r, "A\0B", 3, 22, -1, 0, false); assert(empty(r));
  confirm(r, "Four-byte UTF-8: \xf0\x9f\x93\xbb", 22);
  // Changed text invalidates the display before the full new cycle arrives.
  const uint8_t changed[] = {'N','E','W','!'};
  packet(r, 22, 0, changed, 0); assert(empty(r));
  // A conflicting duplicate invalidates this partial cycle.
  const uint8_t conflict[] = {'B','A','D','!'};
  packet(r, 22, 0, conflict, 0);
  packet(r, 22, 1, late, 0);
  packet(r, 22, 1, changed, 0);
  const uint8_t end[] = {13, 13, 13, 13}; packet(r, 22, 2, end, 0);
  assert(empty(r));
  confirm(r, "Recovered after conflict", 22);
  char text[129];
  memset(text, 'X', 128); text[128] = 0;
  confirm(r, text, 22); // Segment 31 and a message without a terminator.
  text[0] = 0;
  for (int i = 0; i < 42; ++i) strcat(text, "中");
  strcat(text, "AB"); assert(strlen(text) == 128); confirm(r, text, 22);
  rds_decoder_accept(r, 0x4321, 0, 0x4142, 0, 0); assert(rds_enhanced_group(r) == -1 && empty(r));
  rds_decoder_clear(r); announce(r); confirm(r, "eRT", 22);
  for (int repeat = 0; repeat < 2; ++repeat) rds_decoder_accept(r, 0x1234, 0x2000, 0x530d, 0, 0x5244); // Ordinary RT = RDS.
  assert(!strcmp(rds_radio_text(r), "RDS") && !strcmp(rds_enhanced_text(r), "eRT"));
  rds_decoder_clear(r); assert(rds_enhanced_group(r) == -1 && empty(r) && !rds_radio_text(r)[0]);
  puts("eRT announcement/routing, UTF-8, cycle integrity, confirmation, limits and reset passed");
  return 0;
}

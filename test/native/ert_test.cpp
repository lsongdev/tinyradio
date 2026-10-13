#include "drivers/si473x/rds.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <string.h>
using drivers::RdsDecoder;
static void announcement(RdsDecoder& r, uint8_t group = 22, uint16_t flags = 1,
                         uint16_t aid = 0x6552, uint8_t errors = 0) {
  r.accept(0x1234, 0x3000 | group, aid, errors, flags);
}
static void packet(RdsDecoder& r, uint8_t group, uint8_t segment, const uint8_t* bytes, uint8_t errors = 0) {
  r.accept(0x1234, (uint16_t(group) << 11) | segment,
           uint16_t(bytes[2] << 8 | bytes[3]), errors, uint16_t(bytes[0] << 8 | bytes[1]));
}
static void message(RdsDecoder& r, const std::string& text, uint8_t group = 22,
                    int missing = -1, uint8_t errors = 0, bool duplicate = false) {
  assert(text.size() <= 128); uint8_t data[128]; memset(data, 13, sizeof data);
  memcpy(data, text.data(), text.size());
  const int count = text.size() < 128 ? int(text.size() / 4 + 1) : 32;
  for (int segment = 0; segment < count; ++segment) {
    if (segment == missing) continue;
    packet(r, group, segment, data + segment * 4, errors);
    if (duplicate) packet(r, group, segment, data + segment * 4, errors);
  }
}
static void confirm(RdsDecoder& r, const std::string& text, uint8_t group = 22) {
  message(r, text, group); message(r, text, group);
  assert(r.enhancedText() == text);
}
int main() {
  RdsDecoder r;
  message(r, "Not announced"); message(r, "Not announced"); assert(!r.enhancedText()[0]);
  announcement(r,22,1,0x6552,0x0c); assert(r.enhancedGroup() == -1); // Bad config block.
  announcement(r,22,1,0x6552,3); assert(r.enhancedGroup() == -1); // Bad AID block.
  announcement(r,22,1,0x6552,0x30); assert(r.enhancedGroup() == -1);
  announcement(r); assert(r.enhancedGroup() == 22 && r.enhancedFlags() == 1);
  message(r, "AB中文节目 - FM95.9",22,-1,0xaa); assert(!r.enhancedText()[0]);
  announcement(r); // Repeated 3A announcements must not reset confirmation.
  message(r, "AB中文节目 - FM95.9",22,-1,0xaa); assert(!strcmp(r.enhancedText(), "AB中文节目 - FM95.9"));
  announcement(r,24,1,0x4bd7); assert(r.enhancedGroup() == 22); // Unrelated ODA.
  message(r, "Wrong group",24); assert(!strcmp(r.enhancedText(), "AB中文节目 - FM95.9"));
  message(r, "Updated 中文"); assert(!r.enhancedText()[0]);
  message(r, "Updated 中文"); assert(!strcmp(r.enhancedText(), "Updated 中文"));
  message(r, ""); message(r, ""); assert(!r.enhancedText()[0]); // Broadcast clears text.
  announcement(r,24); assert(r.enhancedGroup() == 24);
  message(r,"Old group",22); assert(!r.enhancedText()[0]); confirm(r,"Dynamic group",24);
  announcement(r,24,1,0x4bd7); assert(r.enhancedGroup() == -1 && !r.enhancedText()[0]);
  for (uint16_t flags : {uint16_t(0),uint16_t(3)}) {
    announcement(r,22,flags); message(r,"Unsupported"); message(r,"Unsupported"); assert(!r.enhancedText()[0]);
  }
  for (uint8_t group : {uint8_t(0),uint8_t(4),uint8_t(6),uint8_t(8),uint8_t(23),uint8_t(28),uint8_t(30)}) {
    announcement(r,group); assert(r.enhancedGroup() == -1);
  }
  announcement(r);
  message(r,"Missing middle segment 中文",22,1); message(r,"Missing middle segment 中文",22,1);
  assert(!r.enhancedText()[0]); confirm(r,"Recovered");
  r.clear(); announcement(r);
  const uint8_t late[] = {'L','A','T','E'};packet(r,22,1,late);assert(!r.enhancedText()[0]);
  message(r,"Repeated packets 中文",22,-1,0,true); message(r,"Repeated packets 中文",22,-1,0,true);
  assert(!strcmp(r.enhancedText(),"Repeated packets 中文"));
  for (uint8_t errors : {uint8_t(0xc0),uint8_t(0x30),uint8_t(0x0c),uint8_t(3)}) {
    r.clear();announcement(r);message(r,"Corrupt",22,-1,errors);message(r,"Corrupt",22,-1,errors);
    assert(!r.enhancedText()[0]);
  }
  const char* invalid[] = {"\xc0\xaf", "\xe4\xb8", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe4X\xad", "\x80", "\x1b"};
  for (const char* text : invalid) {
    r.clear();announcement(r);message(r,text);message(r,text);assert(!r.enhancedText()[0]);
  }
  r.clear();announcement(r);message(r,std::string("A\0B",3));message(r,std::string("A\0B",3));assert(!r.enhancedText()[0]);
  confirm(r,"Four-byte UTF-8: \xf0\x9f\x93\xbb");
  // Changed text invalidates the display before the full new cycle arrives.
  const uint8_t changed[] = {'N','E','W','!'};
  packet(r,22,0,changed); assert(!r.enhancedText()[0]);
  // A conflicting duplicate invalidates this partial cycle.
  const uint8_t conflict[] = {'B','A','D','!'};
  packet(r,22,0,conflict);
  packet(r,22,1,late);
  packet(r,22,1,changed);
  const uint8_t end[] = {13,13,13,13}; packet(r,22,2,end);
  assert(!r.enhancedText()[0]);
  confirm(r,"Recovered after conflict");
  confirm(r,std::string(128,'X')); // Segment 31 and a message without a terminator.
  std::string unicode;
  for(int i=0;i<42;++i) unicode += "中";
  unicode += "AB";assert(unicode.size()==128);confirm(r,unicode);
  r.accept(0x4321,0,0x4142,0);assert(r.enhancedGroup()==-1 && !r.enhancedText()[0]);
  r.clear();announcement(r);confirm(r,"eRT");
  for(int repeat=0;repeat<2;++repeat) r.accept(0x1234,0x2000,0x530d,0,0x5244); // Ordinary RT = RDS.
  assert(!strcmp(r.radioText(),"RDS") && !strcmp(r.enhancedText(),"eRT"));
  r.clear();assert(r.enhancedGroup()==-1 && !r.enhancedText()[0] && !r.radioText()[0]);
  puts("eRT announcement/routing, UTF-8, cycle integrity, confirmation, limits and reset passed");
}

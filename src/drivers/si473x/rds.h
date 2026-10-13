#pragma once
#include <stdint.h>
#include <string.h>
namespace drivers {
// Decode PS (0A/0B) and RadioText (2A/2B), confirming two identical messages.
class RdsDecoder {
public:
  void clear() { *this = RdsDecoder(); }
  const char* text() const { return published_; }
  const char* radioText() const { return rtPublished_; }
  void accept(uint16_t pi, uint16_t b, uint16_t d, uint8_t errors, uint16_t c = 0) {
    if ((errors >> 6) > 2) return;
    if (!piKnown_ || pi != pi_) { clear(); pi_ = pi; piKnown_ = true; }
    if (((errors >> 4) & 3) > 2 || (errors & 3) > 2) return;
    if ((b >> 12) == 2) { acceptText(b, d, c, errors); return; }
    if ((b >> 12) != 0) return;
    const uint8_t segment = b & 3;
    candidate_[segment * 2] = printable(d >> 8);
    candidate_[segment * 2 + 1] = printable(d & 255);
    mask_ |= 1 << segment;
    if (mask_ != 15) return;
    if (previousKnown_ && !memcmp(previous_, candidate_, 8)) {
      memcpy(published_, candidate_, 8);
      for (int i = 7; i >= 0 && published_[i] == ' '; --i) published_[i] = 0;
    }
    memcpy(previous_, candidate_, 8); previousKnown_ = true; mask_ = 0;
  }
private:
  void acceptText(uint16_t b, uint16_t d, uint16_t c, uint8_t errors) {
    const bool versionB = b & 0x800, flag = b & 0x10;
    if (!rtKnown_ || flag != rtFlag_ || versionB != rtVersionB_) {
      rtMask_ = 0; rtPreviousKnown_ = false; rtPublished_[0] = 0;
      memset(rtCandidate_, ' ', sizeof rtCandidate_);
      rtKnown_ = true; rtFlag_ = flag; rtVersionB_ = versionB;
    }
    if (!versionB && ((errors >> 2) & 3) > 2) return;
    const int segment = b & 15, width = versionB ? 2 : 4, capacity = versionB ? 32 : 64;
    const uint8_t bytes[] = {uint8_t(c >> 8), uint8_t(c), uint8_t(d >> 8), uint8_t(d)};
    for (int i = 0; i < width; ++i) {
      const uint8_t value = bytes[i + (versionB ? 2 : 0)];
      rtCandidate_[segment * width + i] = value == 13 ? '\r' : printable(value);
    }
    rtMask_ |= uint16_t(1u << segment);
    int length = capacity;
    for (int i = 0; i < capacity; ++i) if (rtCandidate_[i] == '\r') { length = i; break; }
    const int last = length < capacity ? length / width : 15;
    const uint16_t required = uint16_t((1u << (last + 1)) - 1);
    if ((rtMask_ & required) != required) return;
    if (rtPreviousKnown_ && rtPreviousLength_ == length && !memcmp(rtPrevious_, rtCandidate_, length)) {
      memcpy(rtPublished_, rtCandidate_, length); rtPublished_[length] = 0;
      for (int i = length - 1; i >= 0 && rtPublished_[i] == ' '; --i) rtPublished_[i] = 0;
    }
    memcpy(rtPrevious_, rtCandidate_, length); rtPreviousLength_ = length;
    rtPreviousKnown_ = true; rtMask_ = 0;
  }
  uint16_t rtMask_ = 0;
  int rtPreviousLength_ = 0;
  bool rtKnown_ = false, rtFlag_ = false, rtVersionB_ = false, rtPreviousKnown_ = false;
  char rtCandidate_[64]{}, rtPrevious_[64]{}, rtPublished_[65]{};
  static char printable(uint8_t c) { return c >= 32 && c <= 126 ? char(c) : '?'; }
  uint16_t pi_ = 0;
  uint8_t mask_ = 0;
  bool piKnown_ = false, previousKnown_ = false;
  char candidate_[8]{}, previous_[8]{}, published_[9]{};
};
}

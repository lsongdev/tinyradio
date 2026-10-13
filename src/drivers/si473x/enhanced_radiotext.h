#pragma once
#include <stdint.h>
#include <string.h>

namespace drivers {
// eRT ODA 0x6552 on the ordinary RDS stream. A 3A announcement assigns its
// carrier group; group-B variants cannot carry the two text blocks required.
class EnhancedRadioText {
public:
  void clear() { *this = EnhancedRadioText(); }
  const char* text() const { return published_; }
  int group() const { return group_; } // Five-bit type/version code, or -1.
  uint16_t flags() const { return flags_; }
  void announce(uint8_t group, uint16_t flags, uint16_t aid) {
    if (aid != 0x6552) {
      if (group == group_) clear(); // This carrier was reassigned to another ODA.
      return;
    }
    // Only allocatable A groups: never hijack PS, RT, 3A, CT, EON or LPS.
    if ((group & 1) || group < 10 || group > 26) { clear(); return; }
    if (group_ != group || flags_ != flags) {
      clear(); group_ = group; flags_ = flags;
    }
  }
  bool matches(uint8_t group) const { return group_ >= 0 && group == group_; }
  void accept(uint8_t segment, uint16_t c, uint16_t d) {
    // Bit 0 selects UTF-8 rather than legacy UCS-2; bit 1 requests RTL.
    if (!(flags_ & 1) || (flags_ & 2) || segment >= 32) return;
    const uint8_t bytes[] = {uint8_t(c >> 8), uint8_t(c), uint8_t(d >> 8), uint8_t(d)};
    const int offset = segment * 4;
    if (segment == 0) {
      // Consecutive identical retransmissions of a segment are harmless.
      if (active_ && next_ == 1 && !memcmp(candidate_, bytes, 4)) return;
      memset(candidate_, 0, sizeof candidate_); active_ = true; next_ = 0;
    }
    if (!active_) return;
    if (segment != next_) {
      if (segment < next_ && !memcmp(candidate_ + offset, bytes, 4)) return;
      active_ = false; previousKnown_ = false; return;
    }
    // There is no A/B bit. A changed valid segment invalidates old displayed
    // text; confirmation still requires two matching complete transmissions.
    if (published_[0] && previousKnown_ && offset < previousEnd_) {
      const int count = previousEnd_ - offset < 4 ? previousEnd_ - offset : 4;
      if (memcmp(previous_ + offset, bytes, count)) published_[0] = 0;
    }
    memcpy(candidate_ + offset, bytes, 4); ++next_;
    int length = 128;
    for (int i = offset; i < offset + 4; ++i)
      if (candidate_[i] == 0x0d) { length = i; break; }
    if (length == 128 && next_ != 32) return;
    active_ = false;
    if (!validUtf8(candidate_, length)) { previousKnown_ = false; return; }
    if (previousKnown_ && previousLength_ == length && !memcmp(previous_, candidate_, length)) {
      memcpy(published_, candidate_, length); published_[length] = 0;
      for (int i = length - 1; i >= 0 && published_[i] == ' '; --i) published_[i] = 0;
    } else if (published_[0]) published_[0] = 0;
    memcpy(previous_, candidate_, sizeof previous_);
    previousLength_ = length; previousEnd_ = length < 128 ? length + 1 : 128;
    previousKnown_ = true;
  }
private:
  static bool validUtf8(const uint8_t* bytes, int length) {
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
  int group_ = -1;
  uint16_t flags_ = 0;
  bool active_ = false, previousKnown_ = false;
  uint8_t next_ = 0;
  int previousLength_ = 0, previousEnd_ = 0;
  uint8_t candidate_[128]{}, previous_[128]{};
  char published_[129]{};
};
}

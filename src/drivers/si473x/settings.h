#pragma once
#include "tuning.h"
namespace drivers {
struct Bandwidth { uint32_t hz; uint8_t code; };
inline const Bandwidth* bandwidths(Mode mode, int& count) {
  static const Bandwidth fm[] = {{0,0},{40000,4},{60000,3},{84000,2},{110000,1}};
  static const Bandwidth am[] = {{1000,4},{1800,5},{2000,3},{2500,6},{3000,2},{4000,1},{6000,0}};
  static const Bandwidth ssb[] = {{500,4},{1000,5},{1200,0},{2200,1},{3000,2},{4000,3}};
  count = mode == Mode::FM ? 5 : isSsb(mode) ? 6 : 7;
  return mode == Mode::FM ? fm : isSsb(mode) ? ssb : am;
}
struct ReceptionSettings {
  ReceptionSettings(uint8_t bw = 0, int8_t agc = -1, uint8_t sm = 0)
    : bandwidth(bw), gain(agc), softMuteDb(sm) {}
  uint8_t bandwidth = 0; // Index into mode-specific, ascending bandwidth list.
  int8_t gain = -1; // -1: AGC; otherwise manual attenuation index, not dB.
  uint8_t softMuteDb = 0;
};
inline int maxGainIndex(Mode mode) { return mode == Mode::FM ? 26 : 37; }
}

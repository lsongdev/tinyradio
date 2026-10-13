#pragma once
#include <stdint.h>
#include <vector>
#include <deque>
#include <array>
class TwoWire {
public:
  std::vector<std::vector<uint8_t>> commands;
  std::deque<std::array<uint8_t, 13>> fifo;
  bool synced = true;
  void beginTransmission(uint8_t) { tx.clear(); }
  void write(const uint8_t* p, uint8_t n) { tx.assign(p, p + n); }
  int endTransmission() {
    if (!tx.empty()) {
      commands.push_back(tx);
      if (tx[0] == 0x20 || tx[0] == 0x40) carrier = tx[2] << 8 | tx[3];
      if (tx[0] == 0x24 && tx[1] == 3) fifo.clear();
    }
    return 0;
  }
  uint8_t requestFrom(uint8_t, uint8_t size) {
    rx.assign(size, 0); cursor = 0; rx[0] = 0x81;
    if (size == 9 && tx[0] == 0x10) { rx[4] = 0xa9; rx[5] = 2; }
    if (size == 8 && (tx[0] == 0x22 || tx[0] == 0x42)) { rx[2] = carrier >> 8; rx[3] = carrier; }
    if (size == 13) {
      if (tx[1] == 1 && !fifo.empty()) { rx.assign(fifo.front().begin(), fifo.front().end()); fifo.pop_front(); }
      rx[2] = synced ? 1 : 0; rx[3] = fifo.size();
    }
    return size;
  }
  uint8_t read() { return rx.at(cursor++); }
private:
  std::vector<uint8_t> tx, rx;
  uint16_t carrier = 0;
  unsigned cursor = 0;
};

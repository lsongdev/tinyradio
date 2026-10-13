#include "radio.h"
#include "board.h"
#include <Arduino.h>
namespace hal {
void RadioHardware::begin() {
  digitalWrite(board::radioEnable, LOW);
  digitalWrite(board::radioReset, LOW);
  pinMode(board::radioEnable, OUTPUT);
  pinMode(board::radioReset, OUTPUT);
  // I2C pull-ups belong to VCC_RADIO. Leave the bus floating while off.
  pinMode(board::i2cSda, INPUT);
  pinMode(board::i2cScl, INPUT);
}
void RadioHardware::setPowered(bool powered) {
  if (powered == powered_) return;
  if (!powered) {
    Wire.end();
    pinMode(board::i2cSda, INPUT);
    pinMode(board::i2cScl, INPUT);
    digitalWrite(board::radioReset, LOW);
  }
  digitalWrite(board::radioEnable, powered ? HIGH : LOW);
  powered_ = powered;
  if (powered) {
    delay(10);
    Wire.begin(board::i2cSda, board::i2cScl, 100000);
  }
}
void RadioHardware::reset() {
  if (!powered_) return;
  digitalWrite(board::radioReset, LOW); delay(10);
  digitalWrite(board::radioReset, HIGH); delay(10);
}
}

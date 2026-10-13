#include "audio.h"
#include "board.h"
#include <Arduino.h>
namespace hal {
void Audio::begin() {
  digitalWrite(board::audioMute, HIGH);
  digitalWrite(board::amplifierEnable, LOW);
  pinMode(board::audioMute, OUTPUT);
  pinMode(board::amplifierEnable, OUTPUT);
}
void Audio::setMuted(bool muted) { digitalWrite(board::audioMute, muted ? HIGH : LOW); }
void Audio::setSpeakerEnabled(bool enabled) { digitalWrite(board::amplifierEnable, enabled ? HIGH : LOW); }
}

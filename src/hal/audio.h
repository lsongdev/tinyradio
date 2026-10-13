#pragma once
namespace hal {
// Analog SI4732 audio: GPIO controls mute and amp; no MCU DAC/I2S speaker.
class Audio {
public:
  void begin();
  void setMuted(bool muted);
  void setSpeakerEnabled(bool enabled);
};
}

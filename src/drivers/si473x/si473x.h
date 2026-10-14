#pragma once
#include <Wire.h>
#include "tuning.h"
#include "rds.h"
#include "settings.h"
namespace drivers {
enum class Error : uint8_t { None, NoDevice, Bus, Timeout, Command, Frequency, Patch };
enum class SeekResult : uint8_t { None, Searching, Found, Limit, Cancelled, Timeout };
struct Signal {
  uint8_t rssi, snr; bool valid, stereo;
  uint8_t blend, gainIndex;
  bool pilot, softMuted, afcRail, agcEnabled, qualityKnown;
};
struct RdsStatus { bool synchronized = false; uint8_t queued = 0; uint32_t groups = 0, overruns = 0; };
struct Revision { uint8_t part, major, minor, componentMajor, componentMinor, chip; uint16_t patch; };
class Si473x {
public:
  explicit Si473x(TwoWire& bus) : bus_(bus) {}
  // HAL must power and reset the device before begin(). Detects both addresses.
  bool begin();
  bool configure(Mode mode, uint8_t volume);
  bool tune(uint32_t frequencyHz);
  bool seek(bool up, uint32_t lowerHz, uint32_t upperHz, uint32_t spacingHz);
  bool cancelSeek();
  bool seeking() const { return seeking_; }
  SeekResult seekResult() const { return seekResult_; }
  bool poll(); // Nonblocking STC polling; confirmed frequency updated on completion.
  bool readSignal();
  bool readRds();
  const RdsStatus& rdsStatus() const { return rdsStatus_; }
  const RdsClockTime& rdsClockTime() const { return rds_.clockTime(); }
  const char* rdsPs() const { return rds_.text(); }
  const char* rdsText() const { return rds_.radioText(); }
  const char* rdsEnhancedText() const { return rds_.enhancedText(); }
  int rdsEnhancedGroup() const { return rds_.enhancedGroup(); }
  uint16_t rdsEnhancedFlags() const { return rds_.enhancedFlags(); }
  bool setReception(const ReceptionSettings& settings);
  bool setVolume(uint8_t volume);
  bool setMuted(bool muted);
  Error error() const { return error_; }
  const char* errorText() const;
  uint8_t address() const { return address_; }
  Mode mode() const { return mode_; }
  bool tuning() const { return tuning_; }
  bool configured() const { return configured_; }
  uint32_t frequencyHz() const { return confirmedHz_; }
  const Signal& signal() const { return signal_; }
  uint32_t bandwidthHz() const { return bandwidthHz_; }
  const Revision& revision() const { return revision_; }
private:
  bool read(uint8_t* response, uint8_t size);
  bool waitReady(uint32_t timeout = 500);
  bool command(const uint8_t* bytes, uint8_t size, uint8_t* response = nullptr, uint8_t responseSize = 0);
  bool property(uint16_t id, uint16_t value);
  bool powerUp(uint8_t function);
  bool loadSsb();
  bool getRevision();
  bool pollSeek();
  bool seekStatus(bool cancel, bool acknowledge);
  bool fail(Error error);
  bool clearRds();
  TwoWire& bus_;
  uint8_t address_ = 0;
  Error error_ = Error::None;
  Mode mode_ = Mode::FM;
  bool powered_ = false, configured_ = false, tuning_ = false;
  bool carrierKnown_ = false, seeking_ = false;
  SeekResult seekResult_ = SeekResult::None;
  uint32_t seekLowerHz_ = 0, seekUpperHz_ = 0;
  uint16_t carrier_ = 0;
  uint32_t requestedHz_ = 0, confirmedHz_ = 0, tuneStarted_ = 0, lastPoll_ = 0;
  Signal signal_{};
  Revision revision_{};
  RdsDecoder rds_;
  RdsStatus rdsStatus_{};
  uint32_t bandwidthHz_ = 0;
};
}

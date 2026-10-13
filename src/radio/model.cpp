#include "model.h"
namespace radio {
namespace {
constexpr uint32_t fmSteps[] = {10000, 50000, 100000, 200000};
constexpr uint32_t amSteps[] = {1000, 5000, 9000, 10000};
constexpr uint32_t ssbSteps[] = {10, 50, 100, 500, 1000};
int wrap(int value, int size) { return (value % size + size) % size; }
}
Model::Model() {
  for (size_t i = 0; i < bandCount; ++i)
    memories_[i] = {bands[i].initialHz, bands[i].fm ? Mode::FM : Mode::AM,
                   uint8_t(bands[i].fm || i == 1 ? 2 : 0)};
}
uint32_t Model::stepHz() const { return stepForBand(bandIndex_); }
uint32_t Model::stepForBand(size_t index) const {
  const auto& m = memories_[index];
  return m.mode == Mode::FM ? fmSteps[m.step] : drivers::isSsb(m.mode) ? ssbSteps[m.step] : amSteps[m.step];
}
bool Model::setBand(size_t index) {
  if (index >= bandCount || !bands[index].available) return false;
  bandIndex_ = index; return true;
}
bool Model::setMode(Mode mode) {
  if (static_cast<uint8_t>(mode) > 3 || !allowed(band(), mode)) return false;
  auto& m = memories_[bandIndex_];
  if (m.mode == mode) return true;
  m.mode = mode; m.step = drivers::isSsb(mode) ? 2 : (band().fm || bandIndex_ == 1 ? 2 : 0);
  if (!drivers::isSsb(mode)) m.hz = m.hz / 1000 * 1000;
  return true;
}
bool Model::setFrequency(uint32_t hz) {
  drivers::TuneWord word;
  if (hz < band().minHz || hz > band().maxHz || !drivers::encodeFrequency(mode(), hz, word)) return false;
  memories_[bandIndex_].hz = hz; return true;
}
bool Model::turnFrequency(int turns) {
  const int direction = turns < 0 ? -1 : 1;
  int64_t remaining = turns < 0 ? -int64_t(turns) : turns;
  int64_t cycle = 0;
  for (size_t i = 0; i < bandCount; ++i)
    if (bands[i].available) cycle += (bands[i].maxHz - bands[i].minHz) / stepForBand(i) + 1;
  while (remaining) {
    const uint32_t step = stepHz();
    const uint32_t distance = direction > 0 ? band().maxHz - frequencyHz() : frequencyHz() - band().minHz;
    const int64_t room = distance / step;
    if (remaining <= room)
      return setFrequency(int64_t(frequencyHz()) + direction * remaining * step);
    memories_[bandIndex_].hz = int64_t(frequencyHz()) + direction * room * step;
    remaining -= room + 1;
    turnBand(direction); // Skip unavailable bands and restore target mode/step.
    memories_[bandIndex_].hz = direction > 0 ? band().minHz : band().maxHz;
    if (remaining >= cycle) {
      // Skip complete cycles while preserving the same per-band memories as
      // single-detent traversal. Bounds integer overflow and processing time.
      for (size_t i = 0; i < bandCount; ++i) {
        if (!bands[i].available) continue;
        const uint32_t span = (bands[i].maxHz - bands[i].minHz) / stepForBand(i) * stepForBand(i);
        memories_[i].hz = direction > 0 ? bands[i].minHz + span : bands[i].maxHz - span;
      }
      memories_[bandIndex_].hz = direction > 0 ? band().minHz : band().maxHz;
      remaining %= cycle;
    }
  }
  return true;
}
void Model::turnBand(int turns) {
  turns %= 4; // Four available bands; avoid unbounded work for accumulated input.
  int index = bandIndex_;
  const int direction = turns < 0 ? -1 : 1;
  for (int i = 0; i < (turns < 0 ? -int64_t(turns) : turns); ++i) {
    do { index = wrap(index + direction, bandCount); } while (!bands[index].available);
  }
  setBand(index);
}
void Model::turnMode(int turns) {
  turns %= band().fm ? 1 : 3;
  int mode = static_cast<int>(this->mode());
  const int direction = turns < 0 ? -1 : 1;
  for (int i = 0; i < (turns < 0 ? -int64_t(turns) : turns); ++i) {
    do { mode = wrap(mode + direction, 4); } while (!allowed(band(), static_cast<Mode>(mode)));
  }
  setMode(static_cast<Mode>(mode));
}
void Model::turnStep(int turns) {
  auto& m = memories_[bandIndex_];
  m.step = wrap(m.step + turns, drivers::isSsb(m.mode) ? 5 : 4);
}
}

#include "model.h"
namespace radio {
namespace {
constexpr uint32_t fmSteps[] = {10000, 50000, 100000, 200000};
constexpr uint32_t amSteps[] = {1000, 5000, 9000, 10000};
constexpr uint32_t ssbSteps[] = {10, 50, 100, 500, 1000};
constexpr uint32_t swAmSteps[] = {1000, 5000, 9000, 10000, 100000};
constexpr uint32_t swSsbSteps[] = {10, 50, 100, 500, 1000, 10000, 100000};
int wrap(int value, int size) { return (value % size + size) % size; }
}
uint32_t stepOption(size_t bandIndex, Mode mode, int index) {
  if (mode == Mode::FM) return index >= 0 && index < 4 ? fmSteps[index] : 0;
  if (bandIndex == 2 && drivers::isSsb(mode)) return index >= 0 && index < 7 ? swSsbSteps[index] : 0;
  if (bandIndex == 2) return index >= 0 && index < 5 ? swAmSteps[index] : 0;
  if (drivers::isSsb(mode)) return index >= 0 && index < 5 ? ssbSteps[index] : 0;
  return index >= 0 && index < 4 ? amSteps[index] : 0;
}
uint8_t stepOptionCount(size_t bandIndex, Mode mode) {
  if (mode == Mode::FM) return 4;
  if (bandIndex == 2 && drivers::isSsb(mode)) return 7;
  if (bandIndex == 2) return 5;
  return drivers::isSsb(mode) ? 5 : 4;
}
int stepOptionIndex(size_t bandIndex, Mode mode, uint32_t stepHz) {
  for (int i = 0; i < stepOptionCount(bandIndex, mode); ++i)
    if (stepOption(bandIndex, mode, i) == stepHz) return i;
  return -1;
}
Model::Model() {
  for (size_t i = 0; i < bandCount; ++i)
    memories_[i] = {bands[i].initialHz, bands[i].fm ? Mode::FM : Mode::AM,
                   uint8_t(bands[i].fm || i == 1 ? 2 : 0)};
}
Model::State Model::state() const {
  State result{}; result.band = bandIndex_;
  for (size_t i = 0; i < bandCount; ++i)
    result.memories[i] = {memories_[i].hz, memories_[i].mode, stepForBand(i)};
  return result;
}
bool Model::restore(const State& state) {
  if (state.band >= bandCount || !bands[state.band].available) return false;
  Model candidate;
  for (size_t i = 0; i < bandCount; ++i) {
    if (!bands[i].available) continue;
    const auto& memory = state.memories[i];
    if (!candidate.setBand(i) || !candidate.setMode(memory.mode) || !candidate.setFrequency(memory.frequencyHz)) return false;
    const int stepIndex = stepOptionIndex(i, memory.mode, memory.stepHz);
    if (stepIndex < 0) return false;
    const int currentStepIndex = stepOptionIndex(i, memory.mode, candidate.stepHz());
    if (currentStepIndex < 0) return false;
    candidate.turnStep(stepIndex - currentStepIndex);
  }
  candidate.setBand(state.band); *this = candidate; return true;
}
uint32_t Model::stepHz() const { return stepForBand(bandIndex_); }
uint32_t Model::stepForBand(size_t index) const {
  const auto& m = memories_[index];
  return stepOption(index, m.mode, m.step);
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
  while (remaining) {
    const uint32_t step = stepHz();
    const uint32_t distance = direction > 0 ? band().maxHz - frequencyHz() : frequencyHz() - band().minHz;
    const int64_t room = distance / step;
    if (remaining <= room)
      return setFrequency(int64_t(frequencyHz()) + direction * remaining * step);
    memories_[bandIndex_].hz = int64_t(frequencyHz()) + direction * room * step;
    remaining -= room + 1;
    int next = int(bandIndex_) + direction;
    while (next >= 0 && next < int(bandCount) && !bands[next].available) next += direction;
    if (next < 0 || next >= int(bandCount))
      return setFrequency(direction > 0 ? band().maxHz : band().minHz);
    setBand(next); // Cross adjacent bands, but never join the global endpoints.
    memories_[bandIndex_].hz = direction > 0 ? band().minHz : band().maxHz;
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
  m.step = wrap(m.step + turns, stepOptionCount(bandIndex_, m.mode));
}
}

#include "StepTransport.h"

namespace {

char padGlyph(uint8_t pad) {
  if (pad < 10) {
    return '0' + pad;
  }
  return 'A' + (pad - 10);
}

}  // namespace

void StepTransport::begin(uint16_t bpm) {
  for (uint8_t i = 0; i < kStepCount; i++) {
    steps_[i] = StepSlot();
  }
  setBpm(bpm);
  playing_ = false;
  recording_ = false;
  currentStep_ = kStepCount - 1;
  lastStepAtMs_ = 0;
}

bool StepTransport::toggleStep(uint8_t step, uint8_t pad) {
  if (step < 1 || step > kStepCount) {
    return false;
  }
  if (pad < 1 || pad > PadSampleEngine::kPadCount) {
    pad = 1;
  }
  StepSlot &slot = steps_[step - 1];
  slot.active = !slot.active;
  slot.pad = pad;
  return slot.active;
}

void StepTransport::recordPad(uint8_t pad) {
  if (pad < 1 || pad > PadSampleEngine::kPadCount) {
    return;
  }
  uint8_t target = playing_ ? currentStep_ : 0;
  steps_[target].active = true;
  steps_[target].pad = pad;
}

bool StepTransport::tick(PadSampleEngine &engine, String &eventText) {
  if (!playing_) {
    return false;
  }

  uint32_t now = millis();
  uint32_t interval = stepIntervalMs();
  if (lastStepAtMs_ != 0 && now - lastStepAtMs_ < interval) {
    return false;
  }

  lastStepAtMs_ = now;
  currentStep_ = (currentStep_ + 1) % kStepCount;
  if (!steps_[currentStep_].active) {
    return false;
  }

  bool accepted = engine.trigger(steps_[currentStep_].pad, eventText);
  eventText = String("Step ") + String(currentStep_ + 1) + " " + eventText;
  return accepted;
}

void StepTransport::play() {
  playing_ = true;
  currentStep_ = kStepCount - 1;
  lastStepAtMs_ = 0;
}

void StepTransport::stop() {
  playing_ = false;
  lastStepAtMs_ = 0;
}

void StepTransport::toggleRecord() {
  recording_ = !recording_;
}

bool StepTransport::setBpm(uint16_t bpm) {
  if (bpm < 40 || bpm > 240) {
    return false;
  }
  bpm_ = bpm;
  return true;
}

uint8_t StepTransport::activeCount() const {
  uint8_t active = 0;
  for (uint8_t i = 0; i < kStepCount; i++) {
    if (steps_[i].active) {
      active++;
    }
  }
  return active;
}

String StepTransport::patternString() const {
  String row;
  for (uint8_t i = 0; i < kStepCount; i++) {
    row += steps_[i].active ? padGlyph(steps_[i].pad) : '.';
  }
  return row;
}

String StepTransport::detailString() const {
  return String("Pattern ") + patternString() +
         "|Digits/letters show pad assigned to active steps" +
         "|Step " + String(currentStep()) +
         "|Active " + String(activeCount());
}

uint32_t StepTransport::stepIntervalMs() const {
  uint32_t interval = 60000UL / bpm_ / 4;
  return interval == 0 ? 1 : interval;
}

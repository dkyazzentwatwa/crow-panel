#ifndef CYPHER_TUNE_STEP_TRANSPORT_H
#define CYPHER_TUNE_STEP_TRANSPORT_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "PadSampleEngine.h"

struct StepSlot {
  bool active = false;
  uint8_t pad = 1;
};

class StepTransport {
 public:
  static const uint8_t kStepCount = 16;

  void begin(uint16_t bpm);
  bool toggleStep(uint8_t step, uint8_t pad);
  void recordPad(uint8_t pad);
  bool tick(PadSampleEngine &engine, String &eventText);
  void play();
  void stop();
  void toggleRecord();
  bool playing() const { return playing_; }
  bool recording() const { return recording_; }
  uint16_t bpm() const { return bpm_; }
  bool setBpm(uint16_t bpm);
  uint8_t currentStep() const { return currentStep_ + 1; }
  uint8_t activeCount() const;
  String patternString() const;
  String detailString() const;

 private:
  uint32_t stepIntervalMs() const;

  StepSlot steps_[kStepCount];
  bool playing_ = false;
  bool recording_ = false;
  uint16_t bpm_ = 92;
  uint8_t currentStep_ = kStepCount - 1;
  uint32_t lastStepAtMs_ = 0;
};

#endif

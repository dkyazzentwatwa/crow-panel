#ifndef CYPHER_TUNE_AUDIO_OUTPUT_H
#define CYPHER_TUNE_AUDIO_OUTPUT_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>

class AudioOutput {
 public:
  virtual ~AudioOutput() {}
  virtual bool begin(const HardwareProfile &profile, Stream &log) = 0;
  virtual bool trigger(uint8_t pad, uint8_t velocity) = 0;
  virtual void tick() = 0;
  virtual const char *modeName() const = 0;
  virtual bool hardwareReady() const = 0;
  virtual String statusLine() const = 0;
};

AudioOutput &audioOutput();

#endif

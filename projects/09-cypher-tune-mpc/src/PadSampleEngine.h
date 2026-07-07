#ifndef CYPHER_TUNE_PAD_SAMPLE_ENGINE_H
#define CYPHER_TUNE_PAD_SAMPLE_ENGINE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "AudioOutput.h"
#include "VisualVoices.h"

struct PadSample {
  uint8_t pad;
  const char *label;
  const char *sampleRef;
  uint8_t velocity;
};

class PadSampleEngine {
 public:
  static const uint8_t kPadCount = 16;

  void begin(AudioOutput &audio, VisualVoices &voices);
  bool trigger(uint8_t pad, String &message);
  const PadSample &sample(uint8_t pad) const;
  uint8_t lastPad() const { return lastPad_; }
  const char *lastLabel() const;
  String sampleMap() const;

 private:
  AudioOutput *audio_ = nullptr;
  VisualVoices *voices_ = nullptr;
  uint8_t lastPad_ = 0;
};

#endif

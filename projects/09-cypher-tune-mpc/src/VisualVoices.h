#ifndef CYPHER_TUNE_VISUAL_VOICES_H
#define CYPHER_TUNE_VISUAL_VOICES_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

struct VisualVoice {
  uint8_t pad = 0;
  uint8_t level = 0;
  uint32_t triggeredAtMs = 0;
  const char *label = "idle";
};

class VisualVoices {
 public:
  static const uint8_t kVoiceCount = 4;

  void begin();
  void trigger(uint8_t pad, const char *label, uint8_t velocity);
  void tick();
  uint8_t activeCount() const;
  const VisualVoice &voice(uint8_t index) const;
  String summary() const;
  String detail() const;
  const char *primaryLabel() const;

 private:
  VisualVoice voices_[kVoiceCount];
  uint8_t nextVoice_ = 0;
};

#endif

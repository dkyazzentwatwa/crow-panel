#include "VisualVoices.h"

void VisualVoices::begin() {
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    voices_[i] = VisualVoice();
  }
  nextVoice_ = 0;
}

void VisualVoices::trigger(uint8_t pad, const char *label, uint8_t velocity) {
  VisualVoice &voice = voices_[nextVoice_];
  voice.pad = pad;
  voice.level = constrain(velocity, 1, 127);
  voice.triggeredAtMs = millis();
  voice.label = label;
  nextVoice_ = (nextVoice_ + 1) % kVoiceCount;
}

void VisualVoices::tick() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    uint32_t age = now - voices_[i].triggeredAtMs;
    if (voices_[i].pad == 0 || age < 40) {
      continue;
    }
    uint32_t decayTicks = age / 24;
    uint8_t decay = decayTicks > voices_[i].level ? voices_[i].level : decayTicks;
    voices_[i].level -= decay;
    voices_[i].triggeredAtMs = now;
    if (voices_[i].level == 0) {
      voices_[i].pad = 0;
      voices_[i].label = "idle";
    }
  }
}

uint8_t VisualVoices::activeCount() const {
  uint8_t active = 0;
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    if (voices_[i].pad != 0 && voices_[i].level > 0) {
      active++;
    }
  }
  return active;
}

const VisualVoice &VisualVoices::voice(uint8_t index) const {
  if (index >= kVoiceCount) {
    index = 0;
  }
  return voices_[index];
}

String VisualVoices::summary() const {
  String out;
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    if (i > 0) {
      out += " ";
    }
    if (voices_[i].pad == 0) {
      out += ".";
    } else {
      out += String(voices_[i].pad);
    }
  }
  return out;
}

String VisualVoices::detail() const {
  String out = "Voices";
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    out += String("|V") + String(i + 1) + ": ";
    if (voices_[i].pad == 0) {
      out += "idle";
    } else {
      out += String(voices_[i].label) + " p" + String(voices_[i].pad) +
             " lvl" + String(voices_[i].level);
    }
  }
  return out;
}

const char *VisualVoices::primaryLabel() const {
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    if (voices_[i].pad != 0 && voices_[i].level > 0) {
      return voices_[i].label;
    }
  }
  return "idle";
}

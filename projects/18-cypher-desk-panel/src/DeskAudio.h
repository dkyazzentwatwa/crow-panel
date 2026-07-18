#ifndef CYPHER_DESK_PANEL_AUDIO_H
#define CYPHER_DESK_PANEL_AUDIO_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

class DeskAudio {
 public:
  bool begin();
  void tick();
  void setAmbience(uint8_t ambience);
  void setKeySound(uint8_t sound);
  void setVolume(uint8_t volume);
  void triggerKey();
  uint8_t ambience() const;
  uint8_t keySound() const;
  bool ready() const;
  String status() const;
  const char *ambienceName() const;
  const char *keySoundName() const;

 private:
  uint8_t ambience_ = 0;
  uint8_t keySound_ = 1;
  uint8_t volume_ = 18;
  bool ready_ = false;
  bool ambienceOpen_ = false;
  uint32_t dataStart_ = 0;
  uint32_t dataRemaining_ = 0;
  uint16_t keyFrame_ = 0;
  String status_ = "silent";
  bool openAmbience();
  void closeAmbience();
};

#endif
